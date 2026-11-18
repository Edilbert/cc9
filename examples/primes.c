/* primes.c - sieve of Eratosthenes */
#include <stdio.h>
#include <string.h>

#define N 10000

static char sieve[N + 1];

int main(void)
{
   unsigned i, j, count = 0;
   memset(sieve, 1, sizeof sieve);
   sieve[0] = sieve[1] = 0;
   for (i = 2; i * i <= N; i++)
      if (sieve[i])
         for (j = i * i; j <= N; j += i) sieve[j] = 0;
   for (i = 0; i <= N; i++)
      if (sieve[i])
      {
         if (++count <= 20) printf("%u ", i);
      }
   printf("\n%u primes up to %u\n", count, N);
   return 0;
}
