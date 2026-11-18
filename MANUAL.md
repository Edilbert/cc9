# CC9 Reference Manual

*Deutsche Fassung: [MANUAL_de.md](MANUAL_de.md)*

CC9 is an ANSI C (C89/C90) compiler for the **Sparrow09**, the 6309
homebrew computer. It runs on any host with a standard C compiler
(Linux, macOS, Windows), generates source code for the **BS9**
assembler and runs BS9. The result is a Sparrow file (`.x`) that is
loaded on the Sparrow09 with `LOAD` and started with `RUN`.

---

## Contents

1. [Building and installing](#1-building-and-installing)
2. [Running the compiler](#2-running-the-compiler)
3. [Running programs on the Sparrow09](#3-running-programs-on-the-sparrow09)
4. [Memory layout](#4-memory-layout)
5. [Language](#5-language)
6. [Data types](#6-data-types)
7. [Floating point](#7-floating-point)
8. [The C library](#8-the-c-library)
9. [Calling convention and assembler](#9-calling-convention-and-assembler)
10. [Program size and optimisation](#10-program-size-and-optimisation)
11. [Testing with Specht](#11-testing-with-specht)
12. [Limits and known differences](#12-limits-and-known-differences)
13. [Structure of the compiler](#13-structure-of-the-compiler)

---

## 1. Building and installing

```
make                      # builds cc9
make install              # to /usr/local (PREFIX=... to change)
make WIN=y                # Windows executable with MinGW
```

`make install` copies `cc9` to `$PREFIX/bin`, the library to
`$PREFIX/lib/cc9/lib` and the headers to `$PREFIX/lib/cc9/include`.

CC9 looks for its files relative to its own executable:

| What | Searched in |
|------|-------------|
| library | `<cc9 dir>/lib`, then `<cc9 dir>/../lib/cc9/lib`, or `-L` |
| headers | `<library>/../include`, then `<cc9 dir>/include` |
| BS9 | `<cc9 dir>/bs9`, then `bs9` in `PATH`, or `-B` |

CC9 can be used directly in its source directory without installing.

---

## 2. Running the compiler

```
cc9 [options] file.c ...
```

All given C files and the C library are compiled together into **one**
program. There is no separate linker: CC9 translates everything into
one BS9 source file, includes only the functions and data that are
reachable from the program, and lets BS9 write the Sparrow file.

| Option | Meaning |
|--------|---------|
| `-o file` | output file (default: first source with `.x`) |
| `-S` | write BS9 source (`.as9`) only, do not assemble |
| `-E` | preprocess only, output to stdout |
| `-I path` | additional include directory |
| `-D name[=value]` | define a macro |
| `-U name` | undefine a macro |
| `-L path` | library directory |
| `-B path` | BS9 executable |
| `-funsigned-char` | plain `char` is unsigned (default: signed) |
| `-nostdlib` | compile without the C library |
| `-O0` | switch off the peephole optimiser |
| `-w` | no warnings |
| `-k` | keep `.as9`, `.ls9` and `.sym` after assembling |
| `-v` | show the BS9 command |
| `--org=ADDR` | load address (default `$2000`) |
| `--stack=ADDR` | initial stack pointer (default `$8000`) |
| `--no-header` | no Sparrow header and no BASIC line, plain binary |
| `--version` | print the version |

Addresses may be decimal or start with `$` or `0x`.

```
cc9 hello.c                    # -> hello.x
cc9 -o game.x main.c gfx.c     # several files
cc9 -S -o test.as9 test.c      # look at the assembler output
```

Error messages show the file, the line, the source line and the
position:

```
test.c:4: error: expected ';' or ','
       return x;
       ^
```

Calls of functions that are defined nowhere are reported before
assembling (`undefined reference to 'name'`).

---

## 3. Running programs on the Sparrow09

The `.x` file has the Sparrow format: an 8 byte header (`S`, bank,
load address, length, checksum 0), followed by the BASIC line

```
2026 SYS $200F
```

and the program. On the Sparrow09:

```
LOAD "HELLO.X"
RUN
```

`RUN` executes the BASIC line, `SYS` jumps into the C program. When
`main()` returns (or `exit()` is called) the program returns to BASIC.
The BASIC registers and the BASIC stack are preserved.

With `--no-header` a plain binary without header is written; the entry
point is the load address.

---

## 4. Memory layout

```
$0000-$1FFF  common RAM: BIOS, BASIC, buffers, BASIC stack
$1FF8        Sparrow header (8 bytes, only in the file)
$2000        BASIC line "2026 SYS $200F"
$200F        startup code (__start)
             program code
             initialised data
__eop        end of the file
             uninitialised data (cleared at start)
__end        start of the heap (malloc), grows upwards
   ...
$7FFF        stack, grows downwards (start: --stack, default $8000)
$8000-$FFFF  ROM
```

A program has **24 KB** for code, data, heap and stack. The startup
code

1. saves the BASIC registers and the BASIC stack pointer,
2. sets the C stack to `STACK_TOP`,
3. clears the uninitialised data,
4. calls `main(0, NULL)` and then `exit()`.

**BIOS calls:** the BIOS switches RAM banks. A stack in bank 0 (above
`$2000`) becomes invisible while another bank is selected. The library
therefore switches to the common BASIC stack for every `SWI2` call.
Own assembler code calling the BIOS must do the same or use `__bios()`
from `<sparrow.h>`.

---

## 5. Language

CC9 compiles the complete ANSI C (C89) language:

* all data types including `float`, `double`, `long double`, `struct`,
  `union`, bit-fields, `enum`, `typedef`
* pointers, multidimensional arrays, function pointers, complex
  declarators (`int (*f(int))(int, int)`)
* structures as parameters, return values and in assignments
* initialisers for arrays, structures and unions, nested and partially
  braced, array size from the initialiser (`int a[] = ...`)
* `static` and `extern` at file and block scope, several translation
  units
* functions with a variable number of arguments (`<stdarg.h>`)
* old style (K&R) function definitions and functions without
  prototype with the default argument promotions
* `switch` on `int` and `long` values, `goto`, all loops
* the complete preprocessor: macros with parameters, `#` and `##`,
  `#if`/`#elif` with `defined`, `#include`, `#line`, `#error`,
  `#pragma once`, trigraphs, `__FILE__`, `__LINE__`, `__DATE__`,
  `__TIME__`, `__STDC__`

### Predefined macros

| Macro | Value |
|-------|-------|
| `__STDC__` | 1 |
| `__CC9__` | 1 |
| `__SPARROW09__`, `__6309__`, `__6809__`, `__BIG_ENDIAN__` | 1 |
| `__CHAR_UNSIGNED__` | 1, only with `-funsigned-char` |

### Extensions

* `//` comments
* `$` in identifiers
* inline assembler: `asm("...")` as a statement or at file scope (see
  [section 9](#9-calling-convention-and-assembler))
* `__attribute__((...))` and `__extension__` are ignored
* `#pragma push_macro("name")` / `#pragma pop_macro("name")`
* `long long` is compiled as `long` (32 bit) with a warning
* calling an undeclared function: warning, declaration as `int name()`
  (as specified by C89)

---

## 6. Data types

The 6309 is a big endian CPU. There is no alignment: all types and
structure members may be at any address, structures have no padding.

| Type | Size | Range |
|------|------|-------|
| `char` | 1 | -128..127 (with `-funsigned-char`: 0..255) |
| `signed char` / `unsigned char` | 1 | -128..127 / 0..255 |
| `short`, `int` | 2 | -32768..32767 |
| `unsigned short`, `unsigned` | 2 | 0..65535 |
| `long` | 4 | -2147483648..2147483647 |
| `unsigned long` | 4 | 0..4294967295 |
| `float` | 4 | IEEE 754 binary32 |
| `double`, `long double` | 8 | IEEE 754 binary64 |
| pointer | 2 | |
| `enum` | 2 | like `int` |

`size_t` is `unsigned int`, `ptrdiff_t` and `wchar_t` are `int`.
`L"..."` creates an array of 16 bit characters.

**Bit-fields** are allocated in 16 bit units, starting with the most
significant bit. A bit-field never crosses a unit (at most 16 bits); if
it does not fit into the current unit, a new one is started. `int`
bit-fields are signed. `enum` bit-fields are unsigned if the `enum` has
no negative values (as in GCC).

Integer division and remainder round towards zero; the remainder has
the sign of the dividend. `>>` on negative values shifts
arithmetically. A division by 0 raises the CPU exception (monitor).

---

## 7. Floating point

`float` and `double` are IEEE 754 numbers. The basic operations,
comparisons and conversions are written in assembler (`lib/float.as9`)
and round exactly as required by IEEE 754 (round to nearest even),
including denormals, infinity and NaN. The results are bit identical to
those of a PC; this is checked with random test vectors.

Calculations are done in `double` precision (allowed by C89), `float`
is only a storage format: results of type `float` are rounded to
`float` precision on assignment, on conversion and for `float`
operations.

The floating point routines use static work areas and are therefore
not reentrant: do not use floating point in interrupt routines.

The mathematical functions in `<math.h>` are written in C and accurate
to about 15 digits.

---

## 8. The C library

All C89 headers are available: `assert.h`, `ctype.h`, `errno.h`,
`float.h`, `limits.h`, `locale.h`, `math.h`, `setjmp.h`, `signal.h`,
`stdarg.h`, `stddef.h`, `stdio.h`, `stdlib.h`, `string.h`, `time.h`.
`sparrow.h` adds Sparrow09 specific functions.

### Input and output

* `stdout` and `stderr` write to the console with the BIOS function
  `PUTC` (screen or serial port, depending on the switches). The BIOS
  outputs `'\n'` as a new line.
* `stdin` reads line by line with `GETC`. The input is echoed;
  backspace (`$08` or `$7F`) deletes the last character; RETURN passes
  the line to the program. **Ctrl-D** at the start of a line is end of
  file (`EOF`). `__stdin_echo = 0` (from `<sparrow.h>`) switches off
  the echo.
* `printf`, `fprintf`, `sprintf`, `vprintf` etc. support all C89
  conversions: `%d %i %u %o %x %X %c %s %p %n %% %e %E %f %g %G` with
  flags, width, precision, `*`, `h`, `l`, `L`.
* `scanf`, `fscanf`, `sscanf` with `%d %i %u %o %x %c %s %[ %e %f %g %n %p`,
  `*`, width and `h`, `l`, `L`.
* Floating point output in `printf` and input in `scanf` is linked only
  if the program uses floating point anywhere. A program with `printf`
  has about 3.5 KB without floating point and about 9.5 KB with it.
* There is no file system yet: `fopen()` returns `NULL` with
  `errno = ENOENT`, `remove()` and `rename()` fail.

### Memory

`malloc`, `calloc`, `realloc` and `free` manage the area between the
end of the program (`__end`) and the stack. At least 512 bytes are kept
free for the stack.

### Time

`clock()` returns the 50 Hz counter of the BIOS (`CLOCKS_PER_SEC` =
50). The Sparrow09 has no calendar time for C programs yet: `time()`
returns `(time_t)-1`. `gmtime`, `localtime`, `mktime`, `asctime`,
`ctime`, `strftime` and `difftime` are available and count from 1970.

### Other

* `exit()` calls the functions registered with `atexit()` and returns
  to BASIC; the exit status is in register D.
* `abort()` raises `SIGABRT`. `signal()` and `raise()` manage handlers
  for signals raised with `raise()`.
* `setjmp`/`longjmp` are written in assembler.
* `setlocale()` knows only the `"C"` locale.
* `getenv()` returns `NULL`, `system()` returns -1.
* `rand()` returns values from 0 to 32767 (`RAND_MAX`).

### `<sparrow.h>`

| Function | Meaning |
|----------|---------|
| `int __bios_putc(int c)` | output a character via the BIOS |
| `int __bios_getc(void)` | wait for a key |
| `int __bios_poll(void)` | read a key, -1 if none is pressed |
| `int __bios(int fn, int a, int x)` | any BIOS function (`SWI2`) with B = `fn`, A = low byte of `a`, X = `x`; returns D, X after the call is in `__bios_x` |
| `unsigned long __jiffies(void)` | 50 Hz counter |
| `__stdin_echo` | 1: echo input (default), 0: no echo |
| `PEEK(addr)`, `POKE(addr, value)` | memory access |

---

## 9. Calling convention and assembler

### Registers

| Register | Use |
|----------|-----|
| U | frame pointer, preserved by every function |
| S | stack |
| D, W, X, Y | scratch, not preserved |
| DP | not used and not changed by the compiler |

### Parameters and return values

* Arguments are pushed from right to left; the caller removes them.
* `char`, `short`, `int`, pointers and `enum` use 2 bytes; a `char` is
  in the lower (second) byte.
* `long` uses 4 bytes (big endian), `double` 8 bytes.
* `float` uses 4 bytes if the prototype says `float`, otherwise it is
  passed as `double` (8 bytes).
* Structures and unions are copied to the stack.
* Return values: `char`, `int`, pointers in **D** (`char` sign or zero
  extended), `long` in **Q** (D = high word, W = low word).
* For functions returning `float`, `double` or a structure the caller
  pushes a pointer to a result buffer last. The function writes the
  result there (`float` and `double` as an 8 byte `double`).

### Stack frame

```
_func   PSHS  U
        TFR   S,U
        LEAS  -n,S        ; locals at -1,U ... -n,U
        ...
        TFR   U,S
        PULS  U,PC
```

The first parameter is at `4,U`, with a result buffer at `6,U` (the
buffer pointer is at `4,U`). Functions without parameters and without
local variables have no frame.

### Names

| C | BS9 |
|---|-----|
| global function or variable `name` | `_name` |
| `static` at file scope | `S<n>_name` (n = file number) |
| `static` in a function | `S$<n>_name` |
| runtime routines | `___name` (e.g. `___lmul`, `___dadd_s`) |

### Inline assembler

```c
int twice(int x)
{
   asm("        LDD   4,U\n"
       "        LSLD");          /* result in D */
}
```

The text is copied unchanged into the BS9 file; lines without leading
blanks are labels. At file scope `asm()` may contain complete routines.
Labels defined there are included only if the program uses them.

### Runtime library in assembler

`lib/runtime.as9` (startup, 16/32 bit arithmetic, `setjmp`, BIOS) and
`lib/float.as9` (floating point) consist of units separated by lines
`*=====`. A unit is linked only if one of its labels is used. Other
`.as9` files in the library directory are handled the same way.

---

## 10. Program size and optimisation

With only 24 KB available, CC9 takes care of the code size:

* **Only what is used is linked:** starting from the startup code, CC9
  includes only reachable functions, data and assembler units.
* Floating point support of `printf`/`scanf` is linked only if needed.
* Constant expressions are evaluated at compile time, simple operands
  are used directly as memory operands, `char` comparisons use 8 bit
  instructions, `*p++` uses `LEAX`.
* A peephole optimiser removes redundant jumps, loads, compares and
  dead code (`-O0` switches it off).
* BS9 shortens long branches and `JSR` automatically.

Typical sizes: `printf("Hello")` gives about 3.5 KB, a program with
`printf` and floating point about 9.5 KB; the floating point
operations themselves take about 2 KB.

With `-k` the `.as9`, `.ls9` and `.sym` files are kept;
`tools/sizes.py program.sym` lists the largest functions. With
`spechtrun -p` and `tools/profile.py program.sym` you see in which
functions a program spends its time.

---

## 11. Testing with Specht

`tools/spechtrun` contains a test runner that uses the **Specht**
simulator from the Sparrow09 repository unchanged (`#include
"specht.c"`). It needs no SDL library and no window:

1. It boots the real Sparrow ROM until BASIC waits for a key.
2. It loads the `.x` file like `LOAD`.
3. It types `SYS $xxxx` into the keyboard buffer.
4. Everything the BIOS writes to the console goes to stdout; when the
   program reads from the keyboard, the input comes from stdin (end of
   file as Ctrl-D).
5. When the program returns to BASIC, `spechtrun` exits with the exit
   status of the program (register B).

```
cd tools/spechtrun
make SPECHT=/path/to/sparrow09/software/specht/specht.c
tools/build_rom.sh /path/to/sparrow09 tests     # sparrow.rom + sparrow.sym
tools/spechtrun/spechtrun -r tests/sparrow.rom -y tests/sparrow.sym hello.x
```

| Option | Meaning |
|--------|---------|
| `-r file` | ROM (default `sparrow.rom`) |
| `-y file` | symbol file of the ROM (default `sparrow.sym`) |
| `-n count` | maximum number of executed instructions |
| `-v` | show load address, entry point and instruction count |
| `-t` | show the last instruction addresses after a crash |
| `-p` | profile: executed instructions per address to `specht.prof` |

Exit codes: status of the program, 124 = instruction limit reached,
125 = file cannot be loaded.

**Note on the ROM:** the ROM sources do not assemble with the current
BS9 (see [section 12](#12-limits-and-known-differences)).
`tools/build_rom.sh` therefore works on a copy and adapts it; the
result is byte identical with a ROM built by an older BS9 version.

### Regression tests

```
make test                 # tests/run_tests.sh
CTS=/path/to/c-testsuite make test
```

`tests/run_tests.sh` compiles the programs in `tests/c`, runs them in
Specht and compares the output. With Python and numpy random IEEE test
vectors for all floating point operations are generated and checked.
With `CTS` the C89 tests of the
[c-testsuite](https://github.com/c-testsuite/c-testsuite) are run: 164
of 174 pass; the others require a 32 bit `int`, files, `long long` or
C99/C11/GNU extensions.

---

## 12. Limits and known differences

* No file system in `stdio` (`fopen` returns `NULL`).
* `long long` is 32 bits wide (C89 has no such type).
* No variable length arrays, statement expressions `({...})`,
  `_Generic` or other C99/C11/GNU extensions.
* At most 255 arguments per call.
* BS9 allows 8000 symbols; a program of about 20 KB needs about 1000.
* The floating point and division routines are not reentrant (static
  work areas).
* `printf` prints zeros beyond 17 significant digits instead of the
  exact decimal expansion of the binary value.

### Observations on BS9, the ROM and Specht

These points came up during development:

* The ROM (`software/ROM`) no longer assembles with the current BS9:
  * `bios.as9` (line 783 ff.) defines `K_FLDA` … `K_LOAD` as labels,
    `api.as9` (line 91 ff.) defines the same names as constants. Since
    "Detect duplicate labels" this is an error although the values are
    equal.
  * `FCB` is used with 16 bit values (e.g. `basic.as9` line 569,
    `monitor.as9` line 76). Since "Align n,PC and FCB with lwasm" this
    is an error; `BYTE` produces the same code.
* `software/asm/hello.as9` calls the macro as `CALL    PUTS`; the new
  macro syntax requires `CALL(PUTS)`.
* Specht: `DIVQ` sets N and Z from the low byte of W (`Set_Byte_Flags`)
  and does not detect an overflow of the signed quotient. CC9 does not
  use these flags.

---

## 13. Structure of the compiler

| File | Content |
|------|---------|
| `main.c` | command line, options, reading the library, running BS9 |
| `tokenize.c` | tokenizer, trigraphs, line splicing |
| `preprocess.c` | preprocessor (macro expansion with hide sets) |
| `parse.c` | parser, type checking, constant evaluation, initialisers |
| `type.c` | types and the usual arithmetic conversions |
| `codegen.c` | 6309 code generator, peephole optimiser, linking |
| `lib/` | C library (C) and runtime (`runtime.as9`, `float.as9`) |
| `include/` | headers |
| `tools/spechtrun` | test runner using Specht |
| `tests/` | regression tests |

The structure of tokenizer, preprocessor and parser follows the
pattern of *chibicc* (Rui Ueyama); code generator, library and runtime
were written for the Sparrow09. The floating point algorithms follow
*SoftFloat* (John R. Hauser).

CC9 is released into the public domain (Unlicense, see `LICENSE`).
