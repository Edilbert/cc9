#ifndef _SPARROW_H
#define _SPARROW_H
/* Sparrow09 specific functions */
int  __bios_putc(int c);          /* write a character to the console  */
int  __bios_getc(void);           /* wait for a key                     */
int  __bios_poll(void);           /* key or -1 if none is available     */
unsigned long __jiffies(void);    /* 50 Hz tick counter                 */
/* any BIOS function (SWI2): B = fn, A = low byte of a, X = x
   returns D, X after the call is stored in __bios_x                    */
int  __bios(int fn, int a, int x);
extern int __bios_x;
/* BIOS function numbers (api.as9) */
#define BIOS_PUTC    0
#define BIOS_GETC    1
#define BIOS_GETS    2
#define BIOS_PUTS    3
#define BIOS_FLUSH   4
#define BIOS_SETLED  5
extern int __stdin_echo;          /* 1: echo console input (default)    */
#define PEEK(a)    (*(volatile unsigned char *)(a))
#define POKE(a, v) (*(volatile unsigned char *)(a) = (v))
#endif
