/* mandel.c - ASCII Mandelbrot set (floating point) */
#include <stdio.h>

int main(void)
{
   static const char shade[] = " .:-=+*#%@";
   int x, y, i;
   for (y = 0; y < 21; y++)
   {
      double ci = -1.2 + y * 0.12;
      for (x = 0; x < 64; x++)
      {
         double cr = -2.1 + x * 0.045, zr = 0, zi = 0, t;
         for (i = 0; i < 30 && zr * zr + zi * zi < 4.0; i++)
         {
            t = zr * zr - zi * zi + cr;
            zi = 2 * zr * zi + ci;
            zr = t;
         }
         putchar(shade[i == 30 ? 0 : i % 9 + 1]);
      }
      putchar('\n');
   }
   return 0;
}
