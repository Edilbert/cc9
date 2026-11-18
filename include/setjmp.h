#ifndef _SETJMP_H
#define _SETJMP_H
/* U (frame pointer), S, return address */
typedef unsigned jmp_buf[3];
int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
#endif
