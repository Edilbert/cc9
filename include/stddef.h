#ifndef _STDDEF_H
#define _STDDEF_H
#ifndef NULL
#define NULL ((void *)0)
#endif
typedef int ptrdiff_t;
#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif
#ifndef _WCHAR_T
#define _WCHAR_T
typedef int wchar_t;
#endif
#define offsetof(type, member) ((size_t)&((type *)0)->member)
#endif
