#ifndef _STDARG_H
#define _STDARG_H
/* arguments are pushed right to left; every argument occupies at least
   2 bytes, char values are passed as int (low byte at the higher address) */
typedef char *va_list;
#define va_start(ap, last) ((ap) = (char *)&(last) + sizeof(last))
#define va_arg(ap, type)   (*(type *)(((ap) += sizeof(type)) - sizeof(type)))
#define va_end(ap)         ((void)0)
#endif
