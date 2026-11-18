#include <stdio.h>
#include <stdarg.h>
int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
int apply(int (*f)(int, int), int a, int b) { return f(a, b); }
int sumv(int n, ...)
{
   va_list ap;
   int s = 0;
   va_start(ap, n);
   while (n--) s += va_arg(ap, int);
   va_end(ap);
   return s;
}
long suml(int n, ...)
{
   va_list ap;
   long s = 0;
   va_start(ap, n);
   while (n--) s += va_arg(ap, long);
   va_end(ap);
   return s;
}
double sumd(int n, ...)
{
   va_list ap;
   double s = 0;
   va_start(ap, n);
   while (n--) s += va_arg(ap, double);
   va_end(ap);
   return s;
}
int oldstyle(a, b, c)
   int a;
   char b;
   long c;
{
   return a + b + (int)c;
}
static int counter(void) { static int n; return ++n; }
char cfun(char c) { return c + 1; }
unsigned char ucfun(int x) { return x; }
int (*table[2])(int, int) = { add, sub };
void voidfn(int *p) { *p = 42; return; }
int main(void)
{
   int r, i;
   printf("%d %d\n", apply(add, 3, 4), apply(sub, 3, 4));
   printf("%d %d\n", table[0](10, 5), (*table[1])(10, 5));
   printf("%d\n", sumv(5, 1, 2, 3, 4, 5));
   printf("%ld\n", suml(3, 100000L, 200000L, -50L));
   printf("%.2f\n", sumd(3, 1.5, 2.25, 3.0));
   printf("%d\n", oldstyle(1, 2, 3L));
   for (i = 0; i < 3; i++) counter();
   printf("%d\n", counter());
   printf("%d %d\n", cfun('a'), ucfun(300));
   voidfn(&r);
   printf("%d\n", r);
   {
      int (*fp)(int, int) = add;
      printf("%d\n", fp(1, 2) + (*fp)(3, 4));
   }
   return 0;
}
