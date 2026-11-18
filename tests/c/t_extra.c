#include <stdio.h>
#include <string.h>
#include <stdlib.h>
struct S { int a; long b; char s[5]; };
struct B { unsigned lo : 4, hi : 4; int sg : 5; };
static struct S make(int a) { struct S s; s.a = a; s.b = a * 1000L; strcpy(s.s, "abc"); s.s[0] += (char)a; return s; }
static int geta(struct S s) { return s.a + (int)s.b; }
static struct S pick(int c, struct S x, struct S y) { return c ? x : y; }
static long lsw(long v)
{
   switch (v)
   {
      case 0: return 1;
      case 65536L: return 2;
      case -65536L: return 3;
      case 0x12345678L: return 4;
      default: return -1;
   }
}
static int cnt;
static int side(void) { return ++cnt; }
int main(void)
{
   struct S a, *p = &a, arr[3];
   struct B bits[2], *bp = bits;
   int i, k;
   long acc = 0;
   double d = 0.5;
   *p = make(3);
   printf("%d %ld %s\n", a.a, a.b, a.s);
   arr[1] = make(7);
   printf("%d %d\n", geta(arr[1]), geta(make(2)));
   arr[2] = pick(0, arr[1], a);
   printf("%d %s\n", arr[2].a, pick(1, a, arr[1]).s);
   printf("%c\n", make(5).s[0]);
   memset(bits, 0, sizeof bits);
   for (i = 0; i < 2; i++) { bp[i].lo = i + 9; bp[i].hi = 15 - i; bp[i].sg = -i - 3; }
   bp[1].lo += 7;
   bp->hi--;
   (bp + 1)->sg *= 2;
   for (i = 0; i < 2; i++) printf("%u %u %d\n", bits[i].lo, bits[i].hi, bits[i].sg);
   printf("%ld %ld %ld %ld %ld\n", lsw(0), lsw(65536L), lsw(-65536L), lsw(0x12345678L), lsw(5));
   i = 0;
   do { i++; if (i == 3) continue; acc += i; } while (i < 6);
   printf("%ld\n", acc);
   k = side() + side() * side();
   printf("%d %d\n", k, cnt);
   for (i = 0; i < 4; i++) d = d * 2 + (i & 1 ? 0.25 : -0.125);
   printf("%.5f\n", d);
   {
      char buf[64];
      sprintf(buf, "%s-%05.1f-%-4d|%+ld", "x", 3.14159, 42, 7L);
      printf("%s %d\n", buf, (int)strlen(buf));
   }
   {
      unsigned long u = 0xFFFFFFFFUL;
      long s = -1;
      printf("%d %d %d\n", u > 0UL, s < 0, (unsigned long)s == u);
      u >>= 31; s >>= 31;
      printf("%lu %ld\n", u, s);
   }
   {
      int m[3][3], (*row)[3] = m, *q;
      for (i = 0; i < 9; i++) m[i / 3][i % 3] = i;
      q = &m[0][0];
      printf("%d %d %d\n", row[2][1], *(q + 5), (int)(&m[2][2] - q));
   }
   {
      char *s = malloc(16);
      strcpy(s, "dynamic");
      s = realloc(s, 32);
      strcat(s, " memory");
      printf("%s\n", s);
      free(s);
   }
   return 0;
}
