/* guess.c - number guessing game (keyboard input) */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
   int secret, guess, tries = 0;
   srand((unsigned)clock());
   secret = rand() % 100 + 1;
   printf("I am thinking of a number between 1 and 100.\n");
   for (;;)
   {
      printf("Your guess: ");
      if (scanf("%d", &guess) != 1) break;
      tries++;
      if (guess < secret) printf("Too small.\n");
      else if (guess > secret) printf("Too big.\n");
      else { printf("Right! %d tries.\n", tries); break; }
   }
   return 0;
}
