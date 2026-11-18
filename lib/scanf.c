/*
 * scanf family
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

extern int (*__fp_scanf_ptr)(const char *buf, void *dst, int lng);

static int vfscanf_(FILE *f, const char *fmt, va_list ap)
{
   int count = 0, c, nread = 0;
   char buf[80];
   for (; *fmt; fmt++)
   {
      int suppress = 0, width = 0, lng = 0, half = 0;
      if (isspace((unsigned char)*fmt))
      {
         while ((c = fgetc(f)) != EOF && isspace(c)) nread++;
         if (c != EOF) ungetc(c, f);
         continue;
      }
      if (*fmt != '%' || fmt[1] == '%')
      {
         if (*fmt == '%') fmt++;
         c = fgetc(f);
         if (c == EOF) return count ? count : EOF;
         if (c != (unsigned char)*fmt) { ungetc(c, f); return count; }
         nread++;
         continue;
      }
      fmt++;
      if (*fmt == '*') { suppress = 1; fmt++; }
      while (isdigit((unsigned char)*fmt)) width = width * 10 + *fmt++ - '0';
      if (*fmt == 'h') { half = 1; fmt++; }
      else if (*fmt == 'l') { lng = 1; fmt++; }
      else if (*fmt == 'L') { lng = 2; fmt++; }
      if (*fmt != 'c' && *fmt != '[' && *fmt != 'n')
      {
         while ((c = fgetc(f)) != EOF && isspace(c)) nread++;
         if (c == EOF) return count ? count : EOF;
         ungetc(c, f);
      }
      switch (*fmt)
      {
         case 'n':
            if (!suppress)
            {
               if (lng) *va_arg(ap, long *) = nread;
               else if (half) *va_arg(ap, short *) = (short)nread;
               else *va_arg(ap, int *) = nread;
            }
            break;
         case 'c':
         {
            char *p = suppress ? NULL : va_arg(ap, char *);
            if (!width) width = 1;
            while (width--)
            {
               c = fgetc(f);
               if (c == EOF) return count ? count : EOF;
               nread++;
               if (p) *p++ = (char)c;
            }
            if (!suppress) count++;
            break;
         }
         case 's':
         {
            char *p = suppress ? NULL : va_arg(ap, char *);
            int n = 0;
            if (!width) width = 32767;
            while (n < width && (c = fgetc(f)) != EOF && !isspace(c))
            {
               if (p) *p++ = (char)c;
               n++;
            }
            if (c != EOF && n < width) ungetc(c, f);
            nread += n;
            if (p) *p = 0;
            if (n == 0) return count;
            if (!suppress) count++;
            break;
         }
         case '[':
         {
            char set[256];
            int neg = 0, n = 0;
            char *p = suppress ? NULL : va_arg(ap, char *);
            memset(set, 0, sizeof(set));
            fmt++;
            if (*fmt == '^') { neg = 1; fmt++; }
            if (*fmt == ']') { set[']'] = 1; fmt++; }
            while (*fmt && *fmt != ']')
            {
               if (fmt[1] == '-' && fmt[2] && fmt[2] != ']')
               {
                  int a = (unsigned char)fmt[0], b = (unsigned char)fmt[2];
                  for (; a <= b; a++) set[a] = 1;
                  fmt += 3;
               }
               else set[(unsigned char)*fmt++] = 1;
            }
            if (!width) width = 32767;
            while (n < width && (c = fgetc(f)) != EOF && (set[c] != 0) != neg)
            {
               if (p) *p++ = (char)c;
               n++;
            }
            if (c != EOF && n < width) ungetc(c, f);
            nread += n;
            if (p) *p = 0;
            if (n == 0) return count;
            if (!suppress) count++;
            break;
         }
         case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': case 'p':
         {
            int base = (*fmt == 'd' || *fmt == 'u') ? 10 : *fmt == 'o' ? 8 :
                       *fmt == 'i' ? 0 : 16;
            int n = 0;
            char *end;
            unsigned long v;
            if (!width || width > 79) width = 79;
            c = fgetc(f);
            if ((c == '-' || c == '+') && n < width) { buf[n++] = (char)c; c = fgetc(f); }
            {
               int eb = base;
               if (c == '0' && n < width)
               {
                  buf[n++] = (char)c;
                  c = fgetc(f);
                  if ((c == 'x' || c == 'X') && (base == 16 || base == 0) && n < width)
                  {
                     buf[n++] = (char)c;
                     c = fgetc(f);
                     eb = 16;
                  }
                  else if (base == 0) eb = 8;
               }
               if (eb == 0) eb = 10;
               while (c != EOF && n < width)
               {
                  int dv = isdigit(c) ? c - '0' : isalpha(c) ? (c | 32) - 'a' + 10 : 99;
                  if (dv >= eb) break;
                  buf[n++] = (char)c;
                  c = fgetc(f);
               }
            }
            if (c != EOF) ungetc(c, f);
            buf[n] = 0;
            nread += n;
            if (*fmt == 'd' || *fmt == 'i') v = (unsigned long)strtol(buf, &end, base);
            else v = strtoul(buf, &end, base);
            if (end == buf) return count;
            if (!suppress)
            {
               if (*fmt == 'p') *va_arg(ap, void **) = (void *)(unsigned)v;
               else if (lng) *va_arg(ap, long *) = (long)v;
               else if (half) *va_arg(ap, short *) = (short)v;
               else *va_arg(ap, int *) = (int)v;
               count++;
            }
            break;
         }
         case 'e': case 'f': case 'g': case 'E': case 'G':
         {
            int n = 0, digits = 0;
            if (!width || width > 79) width = 79;
            c = fgetc(f);
            if ((c == '-' || c == '+') && n < width) { buf[n++] = (char)c; c = fgetc(f); }
            while (c != EOF && isdigit(c) && n < width) { buf[n++] = (char)c; c = fgetc(f); digits++; }
            if (c == '.' && n < width)
            {
               buf[n++] = (char)c;
               c = fgetc(f);
               while (c != EOF && isdigit(c) && n < width) { buf[n++] = (char)c; c = fgetc(f); digits++; }
            }
            if (digits && (c == 'e' || c == 'E') && n < width)
            {
               buf[n++] = (char)c;
               c = fgetc(f);
               if ((c == '-' || c == '+') && n < width) { buf[n++] = (char)c; c = fgetc(f); }
               while (c != EOF && isdigit(c) && n < width) { buf[n++] = (char)c; c = fgetc(f); }
            }
            if (c != EOF) ungetc(c, f);
            buf[n] = 0;
            nread += n;
            {
               char dummy[8];
               void *dst = suppress ? (void *)dummy : va_arg(ap, void *);
               if (!__fp_scanf_ptr || !__fp_scanf_ptr(buf, dst, lng)) return count;
               if (!suppress) count++;
            }
            break;
         }
         default:
            return count;
      }
   }
   return count;
}

int scanf(const char *fmt, ...)
{
   va_list ap;
   int n;
   va_start(ap, fmt);
   n = vfscanf_(stdin, fmt, ap);
   va_end(ap);
   return n;
}

int fscanf(FILE *f, const char *fmt, ...)
{
   va_list ap;
   int n;
   va_start(ap, fmt);
   n = vfscanf_(f, fmt, ap);
   va_end(ap);
   return n;
}

int sscanf(const char *s, const char *fmt, ...)
{
   va_list ap;
   FILE f;
   int n;
   f.fd = -1;
   f.flags = 4;
   f.ungot = -1;
   f.str = (char *)s;
   va_start(ap, fmt);
   n = vfscanf_(&f, fmt, ap);
   va_end(ap);
   return n;
}
