#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <locale.h>
#include <errno.h>

int errno;

void __assert_fail(const char *expr, const char *file, int line)
{
   fprintf(stderr, "Assertion failed: %s, file %s, line %d\n", expr, file, line);
   abort();
}

static __sighandler_t handlers[16];

__sighandler_t signal(int sig, __sighandler_t handler)
{
   __sighandler_t old;
   if (sig < 1 || sig > 15) { errno = EINVAL; return SIG_ERR; }
   old = handlers[sig];
   handlers[sig] = handler;
   return old;
}

int raise(int sig)
{
   __sighandler_t h;
   if (sig < 1 || sig > 15) return -1;
   h = handlers[sig];
   if (h == SIG_IGN) return 0;
   if (h == SIG_DFL)
   {
      if (sig == SIGABRT) { fputs("Abort\n", stderr); }
      exit(128 + sig);
   }
   handlers[sig] = SIG_DFL;
   h(sig);
   return 0;
}

static char c_dot[] = ".", c_empty[] = "";
static struct lconv c_lconv = {
   c_dot, c_empty, c_empty, c_empty, c_empty, c_empty, c_empty, c_empty,
   c_empty, c_empty, 127, 127, 127, 127, 127, 127, 127, 127 };

char *setlocale(int category, const char *locale)
{
   static char c_name[] = "C";
   (void)category;
   if (!locale || !*locale || (locale[0] == 'C' && !locale[1])) return c_name;
   return NULL;
}

struct lconv *localeconv(void) { return &c_lconv; }
