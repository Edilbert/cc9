#include <stdio.h>
#include <string.h>
int arr[10];
char buf[32];
int *pa = &arr[3];
char *msg = "global string";
int matrix[3][4];

void fill(int *p, int n, int v) { while (n--) *p++ = v++; }
int sum(int *p, int n) { int s = 0; int i; for (i = 0; i < n; i++) s += p[i]; return s; }
void swap(int *a, int *b) { int t = *a; *a = *b; *b = t; }
int strlen2(const char *s) { const char *p = s; while (*p) p++; return p - s; }

int main(void)
{
   int local[5], i, j, x = 1, y = 2;
   int *p, **pp;
   char *s;
   fill(arr, 10, 100);
   printf("%d %d %d\n", arr[0], arr[9], sum(arr, 10));
   printf("%d %d\n", *pa, pa[2]);
   pa += 2;
   printf("%d %d\n", *pa, (int)(pa - arr));
   for (i = 0; i < 5; i++) local[i] = i * i;
   p = local;
   printf("%d %d %d\n", *p, *(p + 4), p[2]);
   p++;
   printf("%d\n", *p);
   swap(&x, &y);
   printf("%d %d\n", x, y);
   pp = &p;
   **pp = 77;
   printf("%d\n", local[1]);
   strcpy(buf, "hello");
   strcat(buf, " world");
   printf("%s %d %d\n", buf, (int)strlen(buf), strlen2(msg));
   for (s = buf; *s; s++) if (*s == 'o') *s = '0';
   printf("%s\n", buf);
   for (i = 0; i < 3; i++) for (j = 0; j < 4; j++) matrix[i][j] = i * 10 + j;
   printf("%d %d %d\n", matrix[1][2], matrix[2][3], *(*(matrix + 2) + 1));
   {
      int (*row)[4] = matrix;
      printf("%d\n", row[1][3]);
      row++;
      printf("%d\n", (*row)[0]);
   }
   {
      char *names[] = { "alpha", "beta", "gamma" };
      char **np = names;
      printf("%s %s %c\n", names[1], *(np + 2), names[0][2]);
   }
   printf("%d\n", p > local && p < local + 5);
   p = NULL;
   printf("%d\n", p == 0);
   {
      void *v = buf;
      char *c = v;
      printf("%c\n", c[1]);
   }
   {
      int a2[3] = { 1, 2, 3 };
      int *q = &a2[2];
      printf("%d %d\n", *q--, *q);
      printf("%d\n", q[-1]);
   }
   return 0;
}
