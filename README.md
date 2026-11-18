CC9
===

ANSI C (C89) compiler for the Sparrow09 (Hitachi 6309)

A cross compiler for hosts running macOS, Linux or Windows. It
generates source code for the [BS9](https://github.com/edilbert/bs9)
assembler and produces Sparrow files that are started with `LOAD` and
`RUN` on the [Sparrow09](https://codeberg.org/for/sparrow09).

* complete C89 language incl. `float`/`double` (IEEE 754, bit exact),
  structs, unions, bit-fields, K&R functions, full preprocessor
* complete C89 library (stdio on the console, stdlib, string, math,
  setjmp, time, ...)
* only the parts of the program and library that are used are linked
* test runner for the Specht simulator

```
make
./cc9 examples/hello.c        # -> examples/hello.x
```

See [MANUAL.md](MANUAL.md) for the reference manual
(German: [MANUAL_de.md](MANUAL_de.md)).

License
-------

CC9 is released into the public domain ([Unlicense](LICENSE)).
