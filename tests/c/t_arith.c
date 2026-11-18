#include <stdio.h>
int gi = 1234;
unsigned gu = 40000u;
long gl = -123456789L;
unsigned long gul = 3000000000UL;
signed char gc = -5;
unsigned char guc = 250;
short gs = -30000;

int f_div(int a, int b) { return a / b; }
int f_mod(int a, int b) { return a % b; }
unsigned u_div(unsigned a, unsigned b) { return a / b; }
unsigned u_mod(unsigned a, unsigned b) { return a % b; }
long l_div(long a, long b) { return a / b; }
long l_mod(long a, long b) { return a % b; }

int main(void)
{
   int a = 100, b = -7, i;
   unsigned ua = 65000u, ub = 7;
   long la = 1000000L, lb = -3L;
   unsigned long lu = 4000000000UL;
   char c = 'x';
   printf("%d %d %d %d %d\n", a + b, a - b, a * b, a / b, a % b);
   printf("%u %u %u %u\n", ua + ub, ua - ub, ua / ub, ua % ub);
   printf("%ld %ld %ld %ld %ld\n", la + lb, la - lb, la * lb, la / lb, la % lb);
   printf("%lu %lu %lu\n", lu / 3, lu % 7, lu * 3);
   printf("%d %d %d\n", f_div(-100, 7), f_mod(-100, 7), f_div(32767, -1));
   printf("%u %u\n", u_div(65535u, 10), u_mod(65535u, 10));
   printf("%ld %ld\n", l_div(-2000000000L, 7), l_mod(-2000000000L, 7));
   printf("%d %d %u %ld %lu\n", gi, gc, guc, gl, gul);
   printf("%d %d\n", gs, (int)gu);
   printf("%d %d %d\n", c, c + 1, (char)(c + 10));
   printf("%d %d %d %d\n", a << 3, a >> 2, b >> 1, (-1) >> 4);
   printf("%u %u\n", ua >> 3, ua << 1);
   printf("%ld %ld %lu\n", la << 5, lb >> 1, lu >> 9);
   for (i = 0; i < 20; i += 3) printf("%ld ", 1L << i);
   printf("\n");
   for (i = 0; i < 33; i += 4) printf("%lx ", 0x87654321UL >> i);
   printf("\n");
   for (i = 0; i < 32; i += 5) printf("%lx ", (long)0x87654321UL >> i);
   printf("\n");
   printf("%d %d %d\n", a & 0x55, a | 0x101, a ^ 0xff);
   printf("%lx %lx %lx\n", gul & 0xF0F0F0F0UL, gul | 0x0F0FUL, gul ^ 0xFFFF0000UL);
   printf("%d %d %d %d\n", ~a, -a, !a, !0);
   printf("%ld %ld\n", ~la, -la);
   printf("%d %d %d %d %d %d\n", a < b, a > b, a <= 100, a >= 101, a == 100, a != 100);
   printf("%d %d %d\n", ua > 1u, (unsigned)b > 5u, -1 < 0);
   printf("%d %d %d %d\n", la > lb, lu > 1UL, la == 1000000L, lb < -2L);
   printf("%d %d\n", (a > 50 && b < 0), (a < 50 || b > 0));
   a += 5; a -= 2; a *= 3; a /= 4; a %= 50; a <<= 2; a >>= 1; a &= 0xfe; a |= 1; a ^= 3;
   printf("%d\n", a);
   la += 100000L; la *= 3; la /= 7; la -= 1; la <<= 2; la >>= 1;
   printf("%ld\n", la);
   c += 200;
   printf("%d\n", c);
   guc += 10;
   printf("%d\n", guc);
   printf("%d %d %d\n", gi++, gi, ++gi);
   printf("%d %d %d\n", gi--, gi, --gi);
   printf("%ld %ld\n", gl++, ++gl);
   printf("%d\n", (int)sizeof(int) + (int)sizeof(long) * 10 + (int)sizeof(char *) * 100);
   printf("%d %d\n", 7 / 2 * 2, (7 % 3) * (-7 % 3));
   printf("%d\n", (a = 5, a + 1));
   printf("%d %d\n", a ? 10 : 20, b ? (a > 3 ? 1 : 2) : 3);
   {
      long m = 1;
      for (i = 1; i <= 12; i++) m *= i;
      printf("12! = %ld\n", m);
   }
   {
      unsigned int x = 1;
      int n = 0;
      while (x) { x <<= 1; n++; }
      printf("bits %d\n", n);
   }
   return 0;
}
