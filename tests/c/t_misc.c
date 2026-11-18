#include <stdio.h>
#include <string.h>
struct inner { char tag; short vals[3]; };
struct outer { int id; struct inner in[2]; const char *name; };
static struct outer table[] = {
   { 1, { { 'a', { 1, 2, 3 } }, { 'b', { 4 } } }, "first" },
   { 2, { { 'c' } }, "second" },
   { 3 }
};
int grid[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
int partial[5] = { 9, 8 };
char hello[] = "hello";
char sized[10] = "hi";
const char *strs[] = { "x", "yy", "zzz", 0 };
int *iptr = &grid[1][1];
long lvals[] = { 100000L, -1L, 0x7fffffffL };
unsigned char uc_arr[4] = { 255, 128, 0, 1 };
typedef int (*binop)(int, int);
static int mul(int a, int b) { return a * b; }
binop ops[] = { mul };
static int sidx(void) { static int count = 10; return count++; }
int (*getop(int n))(int, int) { (void)n; return mul; }
int recursive_sum(int n) { int local[3]; local[0] = n; if (n == 0) return 0; return local[0] + recursive_sum(n - 1); }
int main(void)
{
   int i, j;
   unsigned char uc = 200;
   signed char sc = -100;
   char big[300];
   printf("%d %c %d %d %s\n", table[0].id, table[0].in[1].tag, table[0].in[1].vals[0], table[0].in[0].vals[2], table[0].name);
   printf("%d %c %d %p\n", table[1].id, table[1].in[0].tag, table[2].id, (void *)table[2].name);
   printf("%d %d\n", (int)(sizeof table / sizeof table[0]), (int)sizeof(struct outer));
   for (i = 0; i < 2; i++) for (j = 0; j < 3; j++) printf("%d", grid[i][j]);
   printf(" %d %d %d\n", partial[1], partial[4], *iptr);
   printf("%s %d %s %d\n", hello, (int)sizeof hello, sized, (int)sizeof sized);
   for (i = 0; strs[i]; i++) printf("%s,", strs[i]);
   printf(" %ld %ld %ld\n", lvals[0], lvals[1], lvals[2]);
   printf("%d %d %d %d\n", uc_arr[0], uc_arr[1], uc + 100, (unsigned char)(uc + 100));
   printf("%d %d %d\n", sc, sc * 2, (signed char)(sc * 2));
   printf("%d %d\n", uc > sc, (int)uc / 3);
   printf("%d %d\n", ops[0](6, 7), getop(0)(3, 3));
   printf("%d %d %d\n", sidx(), sidx(), sidx());
   printf("%d\n", recursive_sum(50));
   memset(big, 1, sizeof big);
   for (i = j = 0; i < 300; i++) j += big[i];
   printf("%d\n", j);
   {
      int x = 5, *p = &x, **pp = &p;
      **pp += 10;
      (*p)++;
      printf("%d\n", x);
   }
   {
      char c;
      int n = 0;
      for (c = 'a'; c <= 'z'; c++) n++;
      printf("%d\n", n);
   }
   {
      unsigned u = 0;
      u--;
      printf("%d\n", u > 1000);
   }
   {
      int a[4], *p = a, k;
      for (k = 0; k < 4; k++) *p++ = k * k;
      p = a;
      printf("%d %d %d\n", *p++, *p++, *++p);
   }
   {
      struct { int a; char b[3]; } s1 = { 7, "ab" }, s2;
      s2 = s1;
      s2.b[0] = 'X';
      printf("%d %s %s\n", s2.a, s1.b, s2.b);
   }
   {
      enum { A = 1, B, C = 10, D } e = D;
      printf("%d %d %d\n", B, e, C + D);
   }
   return 0;
}
