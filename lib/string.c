#include <string.h>
#include <errno.h>

void *memcpy(void *s1, const void *s2, size_t n)
{
   char *d = s1;
   const char *s = s2;
   while (n--) *d++ = *s++;
   return s1;
}

void *memmove(void *s1, const void *s2, size_t n)
{
   char *d = s1;
   const char *s = s2;
   if (d < s) while (n--) *d++ = *s++;
   else if (d > s) { d += n; s += n; while (n--) *--d = *--s; }
   return s1;
}

char *strcpy(char *s1, const char *s2)
{
   char *d = s1;
   while ((*d++ = *s2++) != 0) ;
   return s1;
}

char *strncpy(char *s1, const char *s2, size_t n)
{
   char *d = s1;
   while (n && (*d = *s2) != 0) { d++; s2++; n--; }
   while (n--) *d++ = 0;
   return s1;
}

char *strcat(char *s1, const char *s2)
{
   char *d = s1;
   while (*d) d++;
   while ((*d++ = *s2++) != 0) ;
   return s1;
}

char *strncat(char *s1, const char *s2, size_t n)
{
   char *d = s1;
   while (*d) d++;
   while (n-- && *s2) *d++ = *s2++;
   *d = 0;
   return s1;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
   const unsigned char *a = s1, *b = s2;
   for (; n; n--, a++, b++) if (*a != *b) return *a < *b ? -1 : 1;
   return 0;
}

int strcmp(const char *s1, const char *s2)
{
   const unsigned char *a = (const unsigned char *)s1, *b = (const unsigned char *)s2;
   while (*a && *a == *b) { a++; b++; }
   return *a < *b ? -1 : *a > *b;
}

int strcoll(const char *s1, const char *s2) { return strcmp(s1, s2); }

int strncmp(const char *s1, const char *s2, size_t n)
{
   const unsigned char *a = (const unsigned char *)s1, *b = (const unsigned char *)s2;
   for (; n; n--, a++, b++)
   {
      if (*a != *b) return *a < *b ? -1 : 1;
      if (!*a) break;
   }
   return 0;
}

size_t strxfrm(char *s1, const char *s2, size_t n)
{
   size_t len = strlen(s2);
   if (n) { strncpy(s1, s2, n - 1); s1[n - 1] = 0; }
   return len;
}

void *memchr(const void *s, int c, size_t n)
{
   const unsigned char *p = s;
   for (; n; n--, p++) if (*p == (unsigned char)c) return (void *)p;
   return NULL;
}

char *strchr(const char *s, int c)
{
   for (;; s++)
   {
      if (*s == (char)c) return (char *)s;
      if (!*s) return NULL;
   }
}

size_t strcspn(const char *s1, const char *s2)
{
   size_t n = 0;
   for (; s1[n]; n++) if (strchr(s2, s1[n])) break;
   return n;
}

char *strpbrk(const char *s1, const char *s2)
{
   for (; *s1; s1++) if (strchr(s2, *s1)) return (char *)s1;
   return NULL;
}

char *strrchr(const char *s, int c)
{
   const char *r = NULL;
   for (;; s++)
   {
      if (*s == (char)c) r = s;
      if (!*s) return (char *)r;
   }
}

size_t strspn(const char *s1, const char *s2)
{
   size_t n = 0;
   for (; s1[n]; n++) if (!strchr(s2, s1[n])) break;
   return n;
}

char *strstr(const char *s1, const char *s2)
{
   size_t n = strlen(s2);
   if (!n) return (char *)s1;
   for (; *s1; s1++) if (*s1 == *s2 && !strncmp(s1, s2, n)) return (char *)s1;
   return NULL;
}

char *strtok(char *s1, const char *s2)
{
   static char *last;
   char *tok;
   if (!s1) s1 = last;
   if (!s1) return NULL;
   s1 += strspn(s1, s2);
   if (!*s1) { last = NULL; return NULL; }
   tok = s1;
   s1 += strcspn(s1, s2);
   if (*s1) { *s1 = 0; last = s1 + 1; }
   else last = NULL;
   return tok;
}

void *memset(void *s, int c, size_t n)
{
   char *p = s;
   while (n--) *p++ = (char)c;
   return s;
}

char *strerror(int errnum)
{
   switch (errnum)
   {
      case 0: return "No error";
      case EDOM: return "Math argument out of domain";
      case ERANGE: return "Result out of range";
      case ENOENT: return "No such file or directory";
      case EIO: return "I/O error";
      case EBADF: return "Bad file descriptor";
      case ENOMEM: return "Out of memory";
      case EACCES: return "Permission denied";
      case EINVAL: return "Invalid argument";
      case EMFILE: return "Too many open files";
      case ENOSPC: return "No space left on device";
      case ENOSYS: return "Function not implemented";
   }
   return "Unknown error";
}

size_t strlen(const char *s)
{
   const char *p = s;
   while (*p) p++;
   return p - s;
}
