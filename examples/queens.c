/* queens.c - all solutions of the 8 queens problem */
#include <stdio.h>

static int col[8], diag1[15], diag2[15], solutions;

static void place(int row)
{
   int c;
   if (row == 8) { solutions++; return; }
   for (c = 0; c < 8; c++)
      if (!col[c] && !diag1[row + c] && !diag2[row - c + 7])
      {
         col[c] = diag1[row + c] = diag2[row - c + 7] = 1;
         place(row + 1);
         col[c] = diag1[row + c] = diag2[row - c + 7] = 0;
      }
}

int main(void)
{
   place(0);
   printf("%d solutions\n", solutions);
   return 0;
}
