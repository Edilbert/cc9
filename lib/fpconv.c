/*
 * conversion between double and decimal text
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>

typedef union { double d; struct { unsigned long hi, lo; } w; } dbits;

/* 10^n for n = 1,2,4,...,256 */
static const double pow10_tab[9] = { 1e1, 1e2, 1e4, 1e8, 1e16, 1e32, 1e64, 1e128, 1e256 };

/* x * 10^n */
static double scale10(double x, int n)
{
   int i;
   if (n < 0)
   {
      n = -n;
      for (i = 0; n && i < 9; i++, n >>= 1) if (n & 1) x /= pow10_tab[i];
      while (n > 0) { x /= 1e256; n -= 256; }   /* not reached for doubles */
   }
   else
   {
      for (i = 0; n && i < 9; i++, n >>= 1) if (n & 1) x *= pow10_tab[i];
   }
   return x;
}

/* 17 significant digits of v > 0, returns decimal exponent e: v = d.ddd * 10^e */
static int digits17(double v, char *dig)
{
   int e2, e10, i;
   double x, hd;
   unsigned long hi, lo;
   frexp(v, &e2);
   e10 = (int)((e2 - 1) * 30103L / 100000L);
   if (e2 - 1 < 0 && ((e2 - 1) * 30103L) % 100000L) e10--;
   for (;;)
   {
      if (16 - e10 > 300)
         x = scale10(scale10(v, 300), 16 - e10 - 300);
      else
         x = scale10(v, 16 - e10);
      if (x >= 1e17) { e10++; continue; }
      if (x < 1e16) { e10--; continue; }
      break;
   }
   hd = floor(x / 1e9);
   hi = (unsigned long)hd;
   x -= hd * 1e9;
   if (x < 0) x = 0;
   lo = (unsigned long)(x + 0.5);
   if (lo >= 1000000000UL) { lo -= 1000000000UL; hi++; }
   if (hi >= 100000000UL)
   {
      /* rounding overflow: 10^17 */
      hi = 10000000UL;
      lo = 0;
      e10++;
   }
   for (i = 7; i >= 0; i--) { dig[i] = (char)('0' + hi % 10); hi /= 10; }
   for (i = 16; i >= 8; i--) { dig[i] = (char)('0' + lo % 10); lo /= 10; }
   dig[17] = 0;
   return e10;
}

/* round digit string of length n (keeping n digits) to m digits: returns carry */
static int round_digits(char *dig, int m)
{
   int i, up;
   if (m >= 17) return 0;
   if (m < 0) return 0;
   up = dig[m] > '5';
   if (dig[m] == '5')
   {
      /* exactly half: round to even */
      up = 0;
      for (i = m + 1; i < 17; i++) if (dig[i] != '0') { up = 1; break; }
      if (!up) up = m > 0 && ((dig[m - 1] - '0') & 1);
   }
   if (up)
   {
      for (i = m - 1; i >= 0; i--)
      {
         if (dig[i] < '9') { dig[i]++; break; }
         dig[i] = '0';
      }
      if (i < 0)
      {
         /* all nines: 1000... */
         memmove(dig + 1, dig, 16);
         dig[0] = '1';
         return 1;
      }
   }
   return 0;
}

static char *put_exp(char *p, int e, int conv)
{
   *p++ = (conv == 'E' || conv == 'G') ? 'E' : 'e';
   if (e < 0) { *p++ = '-'; e = -e; }
   else *p++ = '+';
   if (e >= 100) { *p++ = (char)('0' + e / 100); e %= 100; }
   *p++ = (char)('0' + e / 10);
   *p++ = (char)('0' + e % 10);
   return p;
}

#define FL_PLUS  2
#define FL_SPACE 4
#define FL_HASH  8

void __fmt_double(double v, int prec, int conv, int flags, char *buf)
{
   dbits u;
   char dig[20], *p = buf;
   int e, i, neg, exp2;
   int upper = conv == 'E' || conv == 'G' || conv == 'F';
   u.d = v;
   neg = (int)(u.w.hi >> 31);
   exp2 = (int)((u.w.hi >> 20) & 0x7FF);
   if (neg) *p++ = '-';
   else if (flags & FL_PLUS) *p++ = '+';
   else if (flags & FL_SPACE) *p++ = ' ';
   if (exp2 == 0x7FF)
   {
      if ((u.w.hi & 0xFFFFF) | u.w.lo) strcpy(p, upper ? "NAN" : "nan");
      else strcpy(p, upper ? "INF" : "inf");
      return;
   }
   if (neg) v = -v;
   if (v == 0)
   {
      for (i = 0; i < 17; i++) dig[i] = '0';
      dig[17] = 0;
      e = 0;
   }
   else e = digits17(v, dig);

   if (conv == 'g' || conv == 'G')
   {
      int P = prec ? prec : 1, X = e;
      char d2[20];
      strcpy(d2, dig);
      if (v != 0 && round_digits(d2, P)) X++;
      if (P > X && X >= -4)
      {
         conv = conv == 'g' ? 'f' : 'F';
         prec = P - 1 - X;
      }
      else
      {
         conv = conv == 'g' ? 'e' : 'E';
         prec = P - 1;
      }
      if (!(flags & FL_HASH))
      {
         /* format, then strip trailing zeros */
         char *start = p, *q, *dot;
         __fmt_double(v, prec, conv, 0, p);
         dot = strchr(start, '.');
         if (dot)
         {
            char *ex = strchr(start, conv == 'e' || conv == 'f' ? 'e' : 'E');
            char tail[8];
            tail[0] = 0;
            if (ex) { strcpy(tail, ex); *ex = 0; }
            q = start + strlen(start) - 1;
            while (*q == '0') *q-- = 0;
            if (*q == '.') *q = 0;
            strcat(start, tail);
         }
         return;
      }
   }
   if (conv == 'e' || conv == 'E')
   {
      if (v != 0 && round_digits(dig, prec + 1)) e++;
      *p++ = dig[0];
      if (prec > 0 || (flags & FL_HASH)) *p++ = '.';
      for (i = 1; i <= prec; i++) *p++ = i < 17 ? dig[i] : '0';
      p = put_exp(p, e, conv);
      *p = 0;
      return;
   }
   /* f */
   {
      int n = e + 1 + prec;   /* significant digits needed */
      if (v == 0) n = 0;
      if (n < 0)
      {
         /* value < 0.5 * 10^-prec */
         *p++ = '0';
         if (prec > 0 || (flags & FL_HASH)) *p++ = '.';
         for (i = 0; i < prec; i++) *p++ = '0';
         *p = 0;
         return;
      }
      if (n == 0 && v != 0)
      {
         /* only rounding decides: 0.0..0 or 0.0..1 (exact half: even = 0) */
         int up = dig[0] > '5';
         if (dig[0] == '5')
            for (i = 1; i < 17; i++) if (dig[i] != '0') { up = 1; break; }
         if (up)
         {
            memmove(dig + 1, dig, 16);
            dig[0] = '1';
            e++;
         }
         else
         {
            *p++ = '0';
            if (prec > 0 || (flags & FL_HASH)) *p++ = '.';
            for (i = 0; i < prec; i++) *p++ = '0';
            *p = 0;
            return;
         }
      }
      else if (v != 0 && round_digits(dig, n)) e++;
      if (e < 0) *p++ = '0';
      for (i = 0; i <= e; i++) *p++ = i < 17 ? dig[i] : '0';
      if (prec > 0 || (flags & FL_HASH)) *p++ = '.';
      for (i = 0; i < prec; i++)
      {
         int j = e + 1 + i;
         *p++ = (j >= 0 && j < 17) ? dig[j] : '0';
      }
      *p = 0;
   }
}

double strtod(const char *nptr, char **endptr)
{
   const char *s = nptr;
   int neg = 0, any = 0, e = 0, nd = 0, ex = 0, eneg = 0;
   double m = 0;
   while (isspace((unsigned char)*s)) s++;
   if (*s == '-') { neg = 1; s++; }
   else if (*s == '+') s++;
   if ((s[0] | 32) == 'i' && (s[1] | 32) == 'n' && (s[2] | 32) == 'f')
   {
      s += 3;
      if ((s[0] | 32) == 'i' && (s[1] | 32) == 'n' && (s[2] | 32) == 'i' &&
          (s[3] | 32) == 't' && (s[4] | 32) == 'y') s += 5;
      if (endptr) *endptr = (char *)s;
      return neg ? -HUGE_VAL : HUGE_VAL;
   }
   if ((s[0] | 32) == 'n' && (s[1] | 32) == 'a' && (s[2] | 32) == 'n')
   {
      dbits u;
      u.w.hi = 0x7FF80000UL;
      u.w.lo = 0;
      if (endptr) *endptr = (char *)s + 3;
      return u.d;
   }
   for (; isdigit((unsigned char)*s); s++)
   {
      any = 1;
      if (nd < 18) { m = m * 10 + (*s - '0'); if (m) nd++; }
      else e++;
   }
   if (*s == '.')
   {
      s++;
      for (; isdigit((unsigned char)*s); s++)
      {
         any = 1;
         if (nd < 18) { m = m * 10 + (*s - '0'); if (m) nd++; e--; }
      }
   }
   if (!any)
   {
      if (endptr) *endptr = (char *)nptr;
      return 0;
   }
   if (*s == 'e' || *s == 'E')
   {
      const char *t = s + 1;
      if (*t == '-') { eneg = 1; t++; }
      else if (*t == '+') t++;
      if (isdigit((unsigned char)*t))
      {
         for (; isdigit((unsigned char)*t); t++) if (ex < 5000) ex = ex * 10 + (*t - '0');
         s = t;
         e += eneg ? -ex : ex;
      }
   }
   if (endptr) *endptr = (char *)s;
   if (m != 0)
   {
      if (e < -330) m = 0;
      else if (e > 330) m = HUGE_VAL;
      else if (e < -300) m = scale10(scale10(m, -300), e + 300);
      else m = scale10(m, e);
      if (m == HUGE_VAL || m == 0) errno = ERANGE;
   }
   return neg ? -m : m;
}

/* hooks for printf / scanf, linked only when the program uses floating point */
char *__fp_printf(char *ap, int prec, int conv, int flags, char *buf)
{
   __fmt_double(*(double *)ap, prec, conv, flags, buf);
   return ap + 8;
}

int __fp_scanf(const char *buf, void *dst, int lng)
{
   char *end;
   double v = strtod(buf, &end);
   if (end == buf) return 0;
   if (lng) *(double *)dst = v;
   else *(float *)dst = (float)v;
   return 1;
}
