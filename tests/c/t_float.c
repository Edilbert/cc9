#include <stdio.h>
#include <math.h>
#include <float.h>
#include <stdlib.h>
float fsq(float x) { return x * x; }
double avg(double *a, int n) { double s = 0; int i; for (i = 0; i < n; i++) s += a[i]; return s / n; }
struct pt { double x, y; float w; };
struct pt mid(struct pt a, struct pt b) { struct pt r; r.x = (a.x + b.x) / 2; r.y = (a.y + b.y) / 2; r.w = a.w + b.w; return r; }
double gd = 2.5;
float gf = -0.125f;
double darr[4] = { 1.0, 2.0, 3.5, -4.25 };
int main(void)
{
   double x = 2.0, y;
   float f = 1.1f;
   int i;
   long l;
   struct pt a, b, m;
   printf("%f %f\n", sqrt(x), sqrt(1e10));
   printf("%.6f %.6f %.6f\n", sin(1.0), cos(1.0), tan(0.5));
   printf("%.6f %.6f %.6f\n", exp(1.0), log(10.0), log10(1000.0));
   printf("%.6f %.6f\n", pow(2.0, 10.0), pow(2.0, 0.5));
   printf("%.6f %.6f %.6f %.6f\n", atan(1.0) * 4, asin(0.5), acos(0.5), atan2(1.0, -1.0));
   printf("%.6f %.6f %.6f\n", sinh(1.0), cosh(1.0), tanh(0.5));
   printf("%.1f %.1f %.1f %.1f\n", floor(-2.5), ceil(-2.5), floor(2.5), ceil(2.5));
   printf("%.4f %.4f\n", fmod(10.0, 3.0), fabs(-3.25));
   {
      double ip, fp = modf(3.75, &ip);
      int e;
      double mm = frexp(48.0, &e);
      printf("%.2f %.2f %.4f %d %.1f\n", ip, fp, mm, e, ldexp(0.75, 4));
   }
   printf("%f %f\n", fsq(f), (double)f * 2);
   printf("%.3f\n", avg(darr, 4));
   a.x = 1; a.y = 2; a.w = 0.5f;
   b.x = 3; b.y = 6; b.w = 0.25f;
   m = mid(a, b);
   printf("%.2f %.2f %.3f\n", m.x, m.y, m.w);
   printf("%f %f\n", gd * 2, gf);
   y = 0;
   for (i = 0; i < 10; i++) y += 0.1;
   printf("%.15f %d\n", y, y == 1.0);
   l = (long)(123456.789 * 100);
   printf("%ld %d %d\n", l, (int)-3.99, (int)3.99);
   printf("%u\n", (unsigned)60000.0);
   printf("%e %E %g %G\n", 123456.789, 0.000123, 1e-5, 1e20);
   printf("%10.3f|%-10.2e|%+.1f|% .2f\n", 3.14159, 2.71828, 1.0, 2.0);
   printf("%g %g %g %g\n", 100.0, 0.5, 123456789.0, 1.0 / 3.0);
   printf("%.0f %.0f %.0f\n", 2.4, 2.6, 1234567.0);
   printf("%f\n", atof("-12.5e2"));
   printf("%.10g\n", strtod("3.14159265358979", NULL));
   printf("%d %d %d\n", 1.0 < 2.0, 2.0 <= 2.0, -1.0 > 0.0);
   printf("%d %d\n", f > 1.0, f < 1.2f);
   {
      float fa[3];
      fa[0] = 1.5f; fa[1] = fa[0] * 3; fa[2] = fa[1] - 0.25f;
      printf("%.3f %.3f %.3f\n", fa[0], fa[1], fa[2]);
   }
   x = 10;
   x += 2.5; x *= 2; x /= 5; x -= 1;
   printf("%f\n", x);
   x = -x;
   printf("%f %f\n", x, -x);
   i = 7;
   x = i / 2;
   y = i / 2.0;
   printf("%f %f\n", x, y);
   printf("%.3e\n", DBL_MAX);
   printf("%.3e\n", DBL_MIN);
   printf("%g\n", FLT_EPSILON);
   {
      double big = 1e300;
      printf("%e %e\n", big * 10, big * big);
      printf("%f\n", -big * big);
   }
   {
      unsigned long ul = 4000000000UL;
      double d = ul;
      printf("%.1f %lu\n", d, (unsigned long)d);
   }
   return 0;
}
