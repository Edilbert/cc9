/*
 * math library (double precision)
 */
#include <math.h>
#include <errno.h>

typedef union { double d; struct { unsigned long hi, lo; } w; } dbits;

const union __huge_u __huge_val_u = { { 0x7FF00000UL, 0UL } };

static double make_inf(void)
{
   dbits u;
   u.w.hi = 0x7FF00000UL;
   u.w.lo = 0;
   return u.d;
}

static double make_nan(void)
{
   dbits u;
   u.w.hi = 0x7FF80000UL;
   u.w.lo = 0;
   return u.d;
}

static int is_nan(double x) { return x != x; }

static int is_inf(double x)
{
   dbits u;
   u.d = x;
   return (u.w.hi & 0x7FFFFFFFUL) == 0x7FF00000UL && u.w.lo == 0;
}


#define PI      3.14159265358979323846
#define PI_2    1.57079632679489661923
#define PI_4    0.78539816339744830962
#define LN2     0.69314718055994530942
#define LN2_HI  6.93147180369123816490e-01
#define LN2_LO  1.90821492927058770002e-10
#define LOG10E  0.43429448190325182765

double fabs(double x)
{
   dbits u;
   u.d = x;
   u.w.hi &= 0x7FFFFFFFUL;
   return u.d;
}

double frexp(double value, int *exp)
{
   dbits u;
   int e;
   u.d = value;
   e = (int)((u.w.hi >> 20) & 0x7FF);
   if (e == 0)
   {
      if ((u.w.hi & 0x7FFFFFFFUL) == 0 && u.w.lo == 0) { *exp = 0; return value; }
      /* subnormal */
      u.d = value * 18014398509481984.0;   /* 2^54 */
      e = (int)((u.w.hi >> 20) & 0x7FF) - 54;
   }
   else if (e == 0x7FF) { *exp = 0; return value; }
   *exp = e - 1022;
   u.w.hi = (u.w.hi & 0x800FFFFFUL) | 0x3FE00000UL;
   return u.d;
}

double ldexp(double x, int exp)
{
   dbits u;
   int e;
   if (x == 0 || is_nan(x) || is_inf(x)) return x;
   u.d = x;
   e = (int)((u.w.hi >> 20) & 0x7FF);
   if (e == 0)
   {
      u.d = x * 18014398509481984.0;
      e = (int)((u.w.hi >> 20) & 0x7FF) - 54;
   }
   if (exp > 5000) exp = 5000;
   if (exp < -5000) exp = -5000;
   e += exp;
   if (e >= 0x7FF)
   {
      errno = ERANGE;
      return (u.w.hi & 0x80000000UL) ? -make_inf() : make_inf();
   }
   if (e <= 0)
   {
      /* result is subnormal or zero: scale in two steps */
      if (e < -54) { errno = ERANGE; return (u.w.hi & 0x80000000UL) ? -0.0 : 0.0; }
      u.w.hi = (u.w.hi & 0x800FFFFFUL) | ((unsigned long)(e + 54) << 20);
      return u.d / 18014398509481984.0;
   }
   u.w.hi = (u.w.hi & 0x800FFFFFUL) | ((unsigned long)e << 20);
   return u.d;
}

/* round towards zero */
static double trunc_(double x)
{
   dbits u;
   int e;
   u.d = x;
   e = (int)((u.w.hi >> 20) & 0x7FF) - 1023;
   if (e >= 52) return x;
   if (e < 0) { u.w.hi &= 0x80000000UL; u.w.lo = 0; return u.d; }
   if (e >= 20)
   {
      int n = 52 - e;    /* bits to clear in lo: 0..32 */
      if (n >= 32) u.w.lo = 0;
      else u.w.lo &= ~((1UL << n) - 1);
   }
   else
   {
      u.w.lo = 0;
      u.w.hi &= ~((1UL << (20 - e)) - 1);
   }
   return u.d;
}

double modf(double value, double *iptr)
{
   double t;
   if (is_inf(value)) { *iptr = value; return value < 0 ? -0.0 : 0.0; }
   t = trunc_(value);
   *iptr = t;
   return value - t;
}

double floor(double x)
{
   double t;
   if (is_nan(x) || is_inf(x)) return x;
   t = trunc_(x);
   if (t > x) t -= 1;
   return t;
}

double ceil(double x)
{
   double t;
   if (is_nan(x) || is_inf(x)) return x;
   t = trunc_(x);
   if (t < x) t += 1;
   return t;
}

double fmod(double x, double y)
{
   double ax, ay, r;
   int ex, ey;
   if (y == 0 || is_nan(x) || is_nan(y) || is_inf(x)) { errno = EDOM; return make_nan(); }
   if (is_inf(y)) return x;
   ax = fabs(x);
   ay = fabs(y);
   r = ax;
   while (r >= ay)
   {
      double t;
      frexp(r, &ex);
      frexp(ay, &ey);
      t = ldexp(ay, ex - ey);
      if (t > r) t = ldexp(ay, ex - ey - 1);
      r -= t;
   }
   return x < 0 ? -r : r;
}

double sqrt(double x)
{
   double y;
   int e, i;
   if (x < 0) { errno = EDOM; return make_nan(); }
   if (x == 0 || is_nan(x) || is_inf(x)) return x;
   y = frexp(x, &e);          /* x = y * 2^e, y in [0.5,1) */
   if (e & 1) { y *= 2; e--; }  /* y in [0.5,2), e even */
   /* initial approximation */
   y = 0.41731 + 0.59016 * y;
   y = ldexp(y, e / 2);
   for (i = 0; i < 6; i++) y = 0.5 * (y + x / y);
   /* final correction */
   {
      double r = y * y - x;
      if (r != 0) y -= r / (2 * y);
   }
   return y;
}

double exp(double x)
{
   double k, r, s, t;
   int i, n;
   if (is_nan(x)) return x;
   if (x > 709.782712893384) { errno = ERANGE; return make_inf(); }
   if (x < -745.2) { errno = ERANGE; return 0; }
   k = floor(x / LN2 + 0.5);
   r = (x - k * LN2_HI) - k * LN2_LO;      /* |r| <= ln2/2 */
   /* Taylor series of e^r */
   s = 1;
   t = 1;
   for (i = 1; i < 18; i++)
   {
      t *= r / i;
      s += t;
      if (fabs(t) < 1e-17 * s) break;
   }
   n = (int)k;
   return ldexp(s, n);
}

double log(double x)
{
   double m, f, s, s2, t, sum;
   int e, i;
   if (is_nan(x)) return x;
   if (x < 0) { errno = EDOM; return make_nan(); }
   if (x == 0) { errno = ERANGE; return -make_inf(); }
   if (is_inf(x)) return x;
   m = frexp(x, &e);
   if (m < 0.70710678118654752440) { m *= 2; e--; }
   f = m - 1;
   s = f / (2 + f);
   s2 = s * s;
   /* log(1+f) = 2 (s + s^3/3 + s^5/5 + ...) */
   sum = 0;
   t = s;
   for (i = 1; i < 40; i += 2)
   {
      double term = t / i;
      sum += term;
      if (fabs(term) < 1e-18) break;
      t *= s2;
   }
   return e * LN2_HI + (2 * sum + e * LN2_LO);
}

double log10(double x)
{
   return log(x) * LOG10E;
}

static double ipow(double x, long n)
{
   double r = 1;
   int neg = n < 0;
   if (neg) n = -n;
   while (n)
   {
      if (n & 1) r *= x;
      x *= x;
      n >>= 1;
   }
   return neg ? 1 / r : r;
}

double pow(double x, double y)
{
   double yi;
   if (y == 0) return 1;
   if (is_nan(x) || is_nan(y)) return make_nan();
   yi = floor(y);
   if (x == 0)
   {
      if (y > 0) return 0;
      errno = EDOM;
      return make_inf();
   }
   if (yi == y && fabs(y) <= 1024) return ipow(x, (long)y);
   if (x < 0)
   {
      errno = EDOM;
      return make_nan();
   }
   return exp(y * log(x));
}

/* reduce x to [-pi/4, pi/4], returns quadrant */
static double reduce(double x, int *q)
{
   static const double p1 = 1.5707963267341256e+00;
   static const double p2 = 6.0771005065061922e-11;
   static const double p3 = 2.0222662487959506e-21;
   double k = floor(x / PI_2 + 0.5);
   *q = (int)fmod(k, 4.0);
   if (*q < 0) *q += 4;
   return ((x - k * p1) - k * p2) - k * p3;
}

static double sin_poly(double x)
{
   double x2 = x * x, t = x, s = x;
   int i;
   for (i = 2; i < 22; i += 2)
   {
      t *= -x2 / (i * (i + 1));
      s += t;
   }
   return s;
}

static double cos_poly(double x)
{
   double x2 = x * x, t = 1, s = 1;
   int i;
   for (i = 1; i < 21; i += 2)
   {
      t *= -x2 / (i * (i + 1));
      s += t;
   }
   return s;
}

double sin(double x)
{
   int q;
   double r;
   if (is_nan(x) || is_inf(x)) { if (is_inf(x)) errno = EDOM; return make_nan(); }
   r = reduce(x, &q);
   switch (q)
   {
      case 0: return sin_poly(r);
      case 1: return cos_poly(r);
      case 2: return -sin_poly(r);
      default: return -cos_poly(r);
   }
}

double cos(double x)
{
   int q;
   double r;
   if (is_nan(x) || is_inf(x)) { if (is_inf(x)) errno = EDOM; return make_nan(); }
   r = reduce(x, &q);
   switch (q)
   {
      case 0: return cos_poly(r);
      case 1: return -sin_poly(r);
      case 2: return -cos_poly(r);
      default: return sin_poly(r);
   }
}

double tan(double x)
{
   int q;
   double r, s, c;
   if (is_nan(x) || is_inf(x)) { if (is_inf(x)) errno = EDOM; return make_nan(); }
   r = reduce(x, &q);
   s = sin_poly(r);
   c = cos_poly(r);
   if (q & 1) return -c / s;
   return s / c;
}

/* atan for 0 <= x <= tan(pi/8) */
static double atan_small(double x)
{
   double x2 = x * x, t = x, s = x;
   int i;
   for (i = 3; i < 60; i += 2)
   {
      double term;
      t *= -x2;
      term = t / i;
      s += term;
      if (fabs(term) < 1e-18) break;
   }
   return s;
}

double atan(double x)
{
   int neg = x < 0, inv = 0;
   double r;
   if (is_nan(x)) return x;
   if (neg) x = -x;
   if (x > 1) { x = 1 / x; inv = 1; }
   if (x > 0.41421356237309504880) r = PI_4 + atan_small((x - 1) / (x + 1));
   else r = atan_small(x);
   if (inv) r = PI_2 - r;
   return neg ? -r : r;
}

double atan2(double y, double x)
{
   double r;
   if (is_nan(x) || is_nan(y)) return make_nan();
   if (x == 0)
   {
      if (y > 0) return PI_2;
      if (y < 0) return -PI_2;
      return 0;
   }
   r = atan(y / x);
   if (x < 0) r += (y < 0) ? -PI : PI;
   return r;
}

double asin(double x)
{
   if (x > 1 || x < -1) { errno = EDOM; return make_nan(); }
   if (x == 1) return PI_2;
   if (x == -1) return -PI_2;
   return atan(x / sqrt((1 - x) * (1 + x)));
}

double acos(double x)
{
   if (x > 1 || x < -1) { errno = EDOM; return make_nan(); }
   if (x == -1) return PI;
   return 2 * atan(sqrt((1 - x) / (1 + x)));
}

double sinh(double x)
{
   double e;
   if (fabs(x) < 0.5)
   {
      double x2 = x * x, t = x, s = x;
      int i;
      for (i = 2; i < 20; i += 2) { t *= x2 / (i * (i + 1)); s += t; }
      return s;
   }
   e = exp(fabs(x));
   e = 0.5 * (e - 1 / e);
   return x < 0 ? -e : e;
}

double cosh(double x)
{
   double e = exp(fabs(x));
   return 0.5 * (e + 1 / e);
}

double tanh(double x)
{
   double e;
   if (fabs(x) > 20) return x < 0 ? -1.0 : 1.0;
   if (fabs(x) < 0.5) return sinh(x) / cosh(x);
   e = exp(2 * fabs(x));
   e = (e - 1) / (e + 1);
   return x < 0 ? -e : e;
}
