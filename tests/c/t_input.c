#include <stdio.h>
#include <string.h>
int main(void)
{
   char name[40], line[80];
   int a, b, n, c, count = 0;
   double d;
   printf("Name? ");
   if (!fgets(name, sizeof name, stdin)) return 1;
   name[strcspn(name, "\n")] = 0;
   printf("Hello %s\n", name);
   printf("Two numbers? ");
   n = scanf("%d %d", &a, &b);
   printf("n=%d sum=%d\n", n, a + b);
   n = scanf("%lf", &d);
   printf("n=%d d=%.3f\n", n, d * 2);
   while ((c = getchar()) != '\n' && c != EOF) ;
   while (gets(line)) count += (int)strlen(line);
   printf("chars=%d\n", count);
   return 0;
}
