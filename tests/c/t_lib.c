#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <errno.h>
#include <limits.h>

static int cmp_int(const void *a, const void *b)
{
   int x = *(const int *)a, y = *(const int *)b;
   return x < y ? -1 : x > y;
}
static int cmp_str(const void *a, const void *b)
{
   return strcmp(*(char * const *)a, *(char * const *)b);
}
jmp_buf env;
int depth;
void deep(int n) { if (n == 0) longjmp(env, 42); depth++; deep(n - 1); }

int main(void)
{
   int v[12] = { 5, 3, 9, -1, 7, 0, 12, 8, 3, -20, 100, 4 }, i, *key, k;
   char *words[] = { "pear", "apple", "fig", "banana", "cherry" };
   char buf[64], *end, *p;
   long l;
   qsort(v, 12, sizeof(int), cmp_int);
   for (i = 0; i < 12; i++) printf("%d ", v[i]);
   printf("\n");
   k = 7;
   key = bsearch(&k, v, 12, sizeof(int), cmp_int);
   printf("found %d at %d\n", key ? *key : -999, key ? (int)(key - v) : -1);
   k = 6;
   printf("%s\n", bsearch(&k, v, 12, sizeof(int), cmp_int) ? "yes" : "no");
   qsort(words, 5, sizeof(char *), cmp_str);
   for (i = 0; i < 5; i++) printf("%s ", words[i]);
   printf("\n");
   printf("%d %d %ld %ld\n", atoi("  -1234xyz"), atoi("77"), atol("2147483647"), strtol("-0x1F", &end, 16));
   printf("%ld %s\n", strtol("0777", &end, 0), end);
   printf("%lu\n", strtoul("4294967295", NULL, 10));
   errno = 0;
   l = strtol("99999999999", NULL, 10);
   printf("%ld %d\n", l, errno == ERANGE);
   printf("%d %d %d\n", abs(-5), (int)labs(-70000L), div(17, 5).rem);
   printf("%d %d %d %d\n", isalpha('a'), isdigit('5'), isspace('\t'), isupper('a'));
   printf("%c %c %d\n", toupper('q'), tolower('Q'), ispunct('!'));
   strcpy(buf, "The quick brown fox");
   printf("%s|%s|%s\n", strchr(buf, 'q'), strrchr(buf, 'o'), strstr(buf, "brown"));
   printf("%d %d %d\n", (int)strspn(buf, "Teh "), (int)strcspn(buf, "qb"), strncmp(buf, "Thx", 2));
   printf("%d %d %d\n", strcmp("abc", "abd") < 0, strcmp("b", "a") > 0, strcmp("x", "x"));
   p = strtok(buf, " ");
   while (p) { printf("[%s]", p); p = strtok(NULL, " "); }
   printf("\n");
   memset(buf, 'z', 5); buf[5] = 0;
   printf("%s\n", buf);
   memcpy(buf, "abcdef", 7);
   memmove(buf + 2, buf, 4);
   printf("%s %d\n", buf, memcmp("abc", "abd", 3));
   printf("%s\n", (char *)memchr("hello", 'l', 5));
   sprintf(buf, "%05d|%-6s|%x|%c", 42, "ab", 255, 'Z');
   printf("%s\n", buf);
   {
      int a, b; char s[16]; long lv; unsigned u;
      int n = sscanf("12 -34 word 1234567 ff", "%d %d %s %ld %x", &a, &b, s, &lv, &u);
      printf("%d: %d %d %s %ld %u\n", n, a, b, s, lv, u);
      n = sscanf("abc:def", "%[a-z]:%s", s, buf);
      printf("%d %s %s\n", n, s, buf);
   }
   {
      int *m = malloc(100 * sizeof(int)), *m2;
      char *c = calloc(10, 1);
      for (i = 0; i < 100; i++) m[i] = i * 3;
      m = realloc(m, 200 * sizeof(int));
      printf("%d %d %d\n", m[99], c[5], m != NULL);
      m2 = malloc(1000);
      free(m);
      free(c);
      m = malloc(50);
      printf("%d\n", m != NULL && m2 != NULL);
      free(m2);
      free(m);
      for (i = 0; i < 50; i++) { void *q = malloc(300); if (!q) break; free(q); }
      printf("alloc loop %d\n", i);
   }
   if (setjmp(env) == 0) { deep(10); printf("not reached\n"); }
   else printf("longjmp %d\n", depth);
   printf("%d %d %ld\n", INT_MAX, INT_MIN, LONG_MIN);
   {
      int r1 = rand(), r2 = rand();
      srand(1);
      printf("%d\n", r1 == rand() && r2 == rand());
   }
   return 0;
}
