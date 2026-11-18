#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>

void __exit(int status);
char *__heap_base(void);
void __stdio_flush(void);

/* ---------------- exit ---------------- */

static void (*atexit_fn[32])(void);
static int atexit_n;

int atexit(void (*func)(void))
{
   if (atexit_n >= 32) return -1;
   atexit_fn[atexit_n++] = func;
   return 0;
}

void exit(int status)
{
   while (atexit_n > 0) atexit_fn[--atexit_n]();
   __stdio_flush();
   __exit(status);
}

void abort(void)
{
   raise(SIGABRT);
   __exit(EXIT_FAILURE);
}

char *getenv(const char *name) { (void)name; return NULL; }

int system(const char *string) { return string ? -1 : 0; }

/* ---------------- memory ---------------- */

/* block header: size of the block incl. header, next free block */
typedef struct Header { unsigned size; struct Header *next; } Header;

static Header *freelist;
static char   *brk_ptr;

#define STACK_RESERVE 512

static Header *more_core(unsigned n)
{
   char sp;              /* address of a local: approximate stack pointer */
   Header *h;
   if (!brk_ptr) brk_ptr = __heap_base();
   if (n < 64) n = 64;
   if ((unsigned)(&sp - brk_ptr) < n + STACK_RESERVE || &sp < brk_ptr) return NULL;
   h = (Header *)brk_ptr;
   brk_ptr += n;
   h->size = n;
   free((char *)h + sizeof(Header));
   return freelist;
}

void *malloc(size_t size)
{
   Header *p, *prev;
   unsigned n;
   if (size == 0) return NULL;
   if (size > 65535U - 16) return NULL;
   n = (size + sizeof(Header) + 1) & ~1U;
   for (;;)
   {
      prev = NULL;
      for (p = freelist; p; prev = p, p = p->next)
      {
         if (p->size >= n)
         {
            if (p->size >= n + sizeof(Header) + 4)
            {
               Header *q = (Header *)((char *)p + n);
               q->size = p->size - n;
               q->next = p->next;
               p->size = n;
               if (prev) prev->next = q; else freelist = q;
            }
            else
            {
               if (prev) prev->next = p->next; else freelist = p->next;
            }
            p->next = NULL;
            return (char *)p + sizeof(Header);
         }
      }
      if (!more_core(n)) { errno = ENOMEM; return NULL; }
   }
}

/* free list sorted by address, adjacent blocks are merged */
void free(void *ptr)
{
   Header *h, *p, *prev;
   if (!ptr) return;
   h = (Header *)((char *)ptr - sizeof(Header));
   prev = NULL;
   for (p = freelist; p && p < h; prev = p, p = p->next) ;
   h->next = p;
   if (p && (char *)h + h->size == (char *)p)
   {
      h->size += p->size;
      h->next = p->next;
   }
   if (prev)
   {
      prev->next = h;
      if ((char *)prev + prev->size == (char *)h)
      {
         prev->size += h->size;
         prev->next = h->next;
      }
   }
   else freelist = h;
}

void *calloc(size_t nmemb, size_t size)
{
   unsigned long n = (unsigned long)nmemb * size;
   void *p;
   if (n > 65000UL) return NULL;
   p = malloc((size_t)n);
   if (p) memset(p, 0, (size_t)n);
   return p;
}

void *realloc(void *ptr, size_t size)
{
   Header *h;
   void *np;
   unsigned old;
   if (!ptr) return malloc(size);
   if (size == 0) { free(ptr); return NULL; }
   h = (Header *)((char *)ptr - sizeof(Header));
   old = h->size - sizeof(Header);
   if (old >= size) return ptr;
   np = malloc(size);
   if (!np) return NULL;
   memcpy(np, ptr, old);
   free(ptr);
   return np;
}

/* ---------------- numbers ---------------- */

int abs(int j) { return j < 0 ? -j : j; }
long labs(long j) { return j < 0 ? -j : j; }

div_t div(int numer, int denom)
{
   div_t r;
   r.quot = numer / denom;
   r.rem = numer % denom;
   return r;
}

ldiv_t ldiv(long numer, long denom)
{
   ldiv_t r;
   r.quot = numer / denom;
   r.rem = numer % denom;
   return r;
}

static unsigned long next_rand = 1;

int rand(void)
{
   next_rand = next_rand * 1103515245UL + 12345;
   return (int)((next_rand >> 16) & 32767);
}

void srand(unsigned seed) { next_rand = seed; }

static unsigned long conv_ul(const char *nptr, char **endptr, int base, int *ovf, int *neg)
{
   const char *s = nptr;
   unsigned long acc = 0, cutoff;
   int any = 0, c, cutlim;
   *ovf = 0;
   *neg = 0;
   while (isspace((unsigned char)*s)) s++;
   if (*s == '-') { *neg = 1; s++; }
   else if (*s == '+') s++;
   if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
       isxdigit((unsigned char)s[2]))
   {
      s += 2;
      base = 16;
   }
   if (base == 0) base = *s == '0' ? 8 : 10;
   cutoff = ULONG_MAX / (unsigned)base;
   cutlim = (int)(ULONG_MAX % (unsigned)base);
   for (;; s++)
   {
      c = (unsigned char)*s;
      if (isdigit(c)) c -= '0';
      else if (isalpha(c)) c = (c | 32) - 'a' + 10;
      else break;
      if (c >= base) break;
      any = 1;
      if (acc > cutoff || (acc == cutoff && c > cutlim)) *ovf = 1;
      else acc = acc * base + c;
   }
   if (endptr) *endptr = (char *)(any ? s : nptr);
   return acc;
}

unsigned long strtoul(const char *nptr, char **endptr, int base)
{
   int ovf, neg;
   unsigned long v = conv_ul(nptr, endptr, base, &ovf, &neg);
   if (ovf) { errno = ERANGE; return ULONG_MAX; }
   return neg ? -v : v;
}

long strtol(const char *nptr, char **endptr, int base)
{
   int ovf, neg;
   unsigned long v = conv_ul(nptr, endptr, base, &ovf, &neg);
   if (neg)
   {
      if (ovf || v > 2147483648UL) { errno = ERANGE; return LONG_MIN; }
      return (long)(0UL - v);
   }
   if (ovf || v > 2147483647UL) { errno = ERANGE; return LONG_MAX; }
   return (long)v;
}

int atoi(const char *nptr) { return (int)strtol(nptr, NULL, 10); }
long atol(const char *nptr) { return strtol(nptr, NULL, 10); }
double atof(const char *nptr) { return strtod(nptr, NULL); }

/* ---------------- sorting ---------------- */

static void swap_bytes(char *a, char *b, size_t n)
{
   while (n--) { char t = *a; *a++ = *b; *b++ = t; }
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *))
{
   char *b = base;
   size_t i, last;
   while (nmemb > 1)
   {
      /* middle element as pivot */
      swap_bytes(b, b + (nmemb / 2) * size, size);
      last = 0;
      for (i = 1; i < nmemb; i++)
         if (compar(b + i * size, b) < 0) swap_bytes(b + ++last * size, b + i * size, size);
      swap_bytes(b, b + last * size, size);
      /* recurse into the smaller part, loop over the larger one */
      if (last < nmemb - last - 1)
      {
         qsort(b, last, size, compar);
         b += (last + 1) * size;
         nmemb -= last + 1;
      }
      else
      {
         qsort(b + (last + 1) * size, nmemb - last - 1, size, compar);
         nmemb = last;
      }
   }
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *))
{
   const char *b = base;
   while (nmemb > 0)
   {
      const char *p = b + (nmemb / 2) * size;
      int c = compar(key, p);
      if (c == 0) return (void *)p;
      if (c > 0) { b = p + size; nmemb -= nmemb / 2 + 1; }
      else nmemb /= 2;
   }
   return NULL;
}

/* ---------------- multibyte (C locale) ---------------- */

int mblen(const char *s, size_t n)
{
   if (!s) return 0;
   if (!n) return -1;
   return *s ? 1 : 0;
}

int mbtowc(wchar_t *pwc, const char *s, size_t n)
{
   if (!s) return 0;
   if (!n) return -1;
   if (pwc) *pwc = (unsigned char)*s;
   return *s ? 1 : 0;
}

int wctomb(char *s, wchar_t wchar)
{
   if (!s) return 0;
   *s = (char)wchar;
   return 1;
}

size_t mbstowcs(wchar_t *pwcs, const char *s, size_t n)
{
   size_t i;
   for (i = 0; i < n && s[i]; i++) pwcs[i] = (unsigned char)s[i];
   if (i < n) pwcs[i] = 0;
   return i;
}

size_t wcstombs(char *s, const wchar_t *pwcs, size_t n)
{
   size_t i;
   for (i = 0; i < n && pwcs[i]; i++) s[i] = (char)pwcs[i];
   if (i < n) s[i] = 0;
   return i;
}
