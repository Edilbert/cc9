#include <stdio.h>
#define SQ(x) ((x) * (x))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define STR(x) #x
#define XSTR(x) STR(x)
#define CAT(a, b) a##b
#define VERSION 3
#define EMPTY
#if VERSION > 2 && defined(SQ)
#define BIG "yes"
#else
#define BIG "no"
#endif
#ifdef UNDEFINED_THING
#error should not happen
#endif
#ifndef VERSION
#error neither
#elif VERSION == 3
#define THREE 1
#endif
#define f(x) x + g
#define g f
int main(void)
{
   int CAT(var, 1) = 10;
   printf("%d %d %s %s\n", SQ(3 + 1), MAX(4, 9), STR(a + b), XSTR(VERSION));
   printf("%d %s %d\n", var1, BIG, THREE);
   printf("%s %d\n", __FILE__ + 0 != 0 ? "file" : "", __LINE__);
   printf("%d\n", __STDC__);
   printf("%s\n", STR(  spaced   out  ));
   printf("%d\n", SQ(SQ(2)));
#undef SQ
#define SQ 5
   printf("%d EMPTY\n", SQ EMPTY);
   printf("%s\n", "con" "cat" "enated");
   printf("??( ??) ok\n");
   return 0;
}
