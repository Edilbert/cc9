/*
 * printf family
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* set by the compiler when the program uses floating point */
extern char *(*__fp_printf_ptr)(char *ap, int prec, int conv, int flags, char *buf);

#define FL_MINUS 1
#define FL_PLUS  2
#define FL_SPACE 4
#define FL_HASH  8
#define FL_ZERO  16

static int out_count;

static void out(FILE *f, int c)
{
   fputc(c, f);
   out_count++;
}

static void pad(FILE *f, int c, int n)
{
   while (n-- > 0) out(f, c);
}

static int ulong_digits(unsigned long v, int base, int upper, char *buf)
{
   const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
   char tmp[12];
   int n = 0, i;
   do { tmp[n++] = dig[(int)(v % base)]; v /= base; } while (v);
   for (i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
   buf[n] = 0;
   return n;
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
   char buf[352];
   out_count = 0;
   for (; *fmt; fmt++)
   {
      int flags = 0, width = 0, prec = -1, lng = 0, len;
      char *s, prefix[3];
      int plen = 0;
      if (*fmt != '%') { out(f, *fmt); continue; }
      fmt++;
      for (;; fmt++)
      {
         if (*fmt == '-') flags |= FL_MINUS;
         else if (*fmt == '+') flags |= FL_PLUS;
         else if (*fmt == ' ') flags |= FL_SPACE;
         else if (*fmt == '#') flags |= FL_HASH;
         else if (*fmt == '0') flags |= FL_ZERO;
         else break;
      }
      if (*fmt == '*')
      {
         width = va_arg(ap, int);
         if (width < 0) { flags |= FL_MINUS; width = -width; }
         fmt++;
      }
      else while (*fmt >= '0' && *fmt <= '9') width = width * 10 + *fmt++ - '0';
      if (*fmt == '.')
      {
         fmt++;
         prec = 0;
         if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
         else while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + *fmt++ - '0';
      }
      if (*fmt == 'h') fmt++;
      else if (*fmt == 'l') { lng = 1; fmt++; }
      else if (*fmt == 'L') { lng = 2; fmt++; }
      s = buf;
      switch (*fmt)
      {
         case 'd': case 'i':
         {
            long v = lng ? va_arg(ap, long) : (long)va_arg(ap, int);
            unsigned long u = v < 0 ? 0UL - (unsigned long)v : (unsigned long)v;
            if (v < 0) prefix[plen++] = '-';
            else if (flags & FL_PLUS) prefix[plen++] = '+';
            else if (flags & FL_SPACE) prefix[plen++] = ' ';
            len = ulong_digits(u, 10, 0, buf);
            if (prec == 0 && u == 0) len = 0;
            goto integer;
         }
         case 'u': case 'o': case 'x': case 'X':
         {
            unsigned long u = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned);
            int base = *fmt == 'u' ? 10 : *fmt == 'o' ? 8 : 16;
            len = ulong_digits(u, base, *fmt == 'X', buf);
            if (prec == 0 && u == 0) len = 0;
            if ((flags & FL_HASH) && u)
            {
               if (base == 16) { prefix[plen++] = '0'; prefix[plen++] = *fmt; }
            }
            if ((flags & FL_HASH) && base == 8 && (prec <= len) && !(len && buf[0] == '0'))
            {
               if (prec < len + 1) prec = len + 1;
            }
         integer:
         {
            int zeros = prec > len ? prec - len : 0;
            int total = plen + zeros + len;
            if (prec < 0 && (flags & FL_ZERO) && !(flags & FL_MINUS) && width > total)
            {
               zeros += width - total;
               total = width;
            }
            if (!(flags & FL_MINUS)) pad(f, ' ', width - total);
            { int i; for (i = 0; i < plen; i++) out(f, prefix[i]); }
            pad(f, '0', zeros);
            { int i; for (i = 0; i < len; i++) out(f, buf[i]); }
            if (flags & FL_MINUS) pad(f, ' ', width - total);
            break;
         }
         }
         case 'p':
         {
            unsigned u = (unsigned)va_arg(ap, void *);
            len = ulong_digits(u, 16, 0, buf + 1);
            /* 4 hex digits */
            {
               char tmp[8];
               int i;
               strcpy(tmp, buf + 1);
               for (i = 0; i < 4 - len; i++) buf[i] = '0';
               strcpy(buf + i, tmp);
               len = 4;
            }
            prefix[0] = '0'; prefix[1] = 'x'; plen = 2;
            prec = -1;
            flags &= ~FL_ZERO;
            {
               int total = plen + len;
               if (!(flags & FL_MINUS)) pad(f, ' ', width - total);
               out(f, '0'); out(f, 'x');
               { int i; for (i = 0; i < len; i++) out(f, buf[i]); }
               if (flags & FL_MINUS) pad(f, ' ', width - total);
            }
            break;
         }
         case 'c':
            buf[0] = (char)va_arg(ap, int);
            len = 1;
            goto string;
         case 's':
            s = va_arg(ap, char *);
            if (!s) s = "(null)";
            len = 0;
            while (s[len] && (prec < 0 || len < prec)) len++;
         string:
            if (!(flags & FL_MINUS)) pad(f, ' ', width - len);
            { int i; for (i = 0; i < len; i++) out(f, s[i]); }
            if (flags & FL_MINUS) pad(f, ' ', width - len);
            break;
         case 'e': case 'E': case 'f': case 'F': case 'g': case 'G':
         {
            char *p;
            if (prec < 0) prec = 6;
            if (prec > 300) prec = 300;
            if (__fp_printf_ptr) ap = __fp_printf_ptr(ap, prec, *fmt, flags, buf);
            else { ap += 8; strcpy(buf, "?"); }
            p = buf;
            len = strlen(buf);
            if ((flags & FL_ZERO) && !(flags & FL_MINUS) && len < width &&
                buf[len - 1] >= '0' && buf[len - 1] <= '9')
            {
               /* zero padding after the sign */
               int sign = (*p == '-' || *p == '+' || *p == ' ');
               if (sign) out(f, *p++);
               pad(f, '0', width - len);
               for (; *p; p++) out(f, *p);
               break;
            }
            s = buf;
            goto string;
         }
         case 'n':
            if (lng) *va_arg(ap, long *) = out_count;
            else *va_arg(ap, int *) = out_count;
            break;
         case '%':
            out(f, '%');
            break;
         default:
            if (!*fmt) return out_count;
            out(f, '%');
            out(f, *fmt);
            break;
      }
   }
   return out_count;
}

int vprintf(const char *fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }

int printf(const char *fmt, ...)
{
   va_list ap;
   int n;
   va_start(ap, fmt);
   n = vfprintf(stdout, fmt, ap);
   va_end(ap);
   return n;
}

int fprintf(FILE *f, const char *fmt, ...)
{
   va_list ap;
   int n;
   va_start(ap, fmt);
   n = vfprintf(f, fmt, ap);
   va_end(ap);
   return n;
}

int vsprintf(char *s, const char *fmt, va_list ap)
{
   FILE f;
   int n;
   f.fd = -1;
   f.flags = 4;
   f.ungot = -1;
   f.str = s;
   n = vfprintf(&f, fmt, ap);
   *f.str = 0;
   return n;
}

int sprintf(char *s, const char *fmt, ...)
{
   va_list ap;
   int n;
   va_start(ap, fmt);
   n = vsprintf(s, fmt, ap);
   va_end(ap);
   return n;
}
