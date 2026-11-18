#include <stdio.h>
int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
long fact(int n) { return n <= 1 ? 1L : n * fact(n - 1); }
int classify(int x)
{
   switch (x)
   {
      case 0: return 100;
      case 1:
      case 2: return 200;
      case -5: return 300;
      case 1000: return 400;
      default: return -1;
   }
}
const char *lname(long v)
{
   switch (v)
   {
      case 100000L: return "100k";
      case -1L: return "minus one";
      case 70000L: return "70k";
   }
   return "other";
}
int main(void)
{
   int i, j, n = 0;
   for (i = 0; i < 15; i++) printf("%d ", fib(i));
   printf("\n%ld\n", fact(12));
   for (i = -6; i < 4; i++) printf("%d ", classify(i));
   printf("%d\n", classify(1000));
   printf("%s %s %s %s\n", lname(100000L), lname(-1L), lname(70000L), lname(5));
   i = 0;
   while (1) { if (++i > 10) break; if (i % 2) continue; n += i; }
   printf("%d\n", n);
   i = 0;
   do { i += 3; } while (i < 20);
   printf("%d\n", i);
   n = 0;
   for (i = 0; i < 10; i++)
      for (j = 0; j < 10; j++)
      {
         if (j > i) break;
         n += j;
      }
   printf("%d\n", n);
   i = 0;
again:
   i++;
   if (i < 5) goto again;
   printf("goto %d\n", i);
   n = 0;
   switch (3)
   {
      case 1: n += 1;
      case 3: n += 3;
      case 4: n += 4;
         break;
      case 5: n += 5;
   }
   printf("fall %d\n", n);
   for (i = 0, j = 10; i < j; i++, j--) ;
   printf("%d %d\n", i, j);
   {
      int k = 5;
      { int k = 6; printf("%d ", k); }
      printf("%d\n", k);
   }
   return 0;
}
