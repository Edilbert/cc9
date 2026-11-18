# cc9 - ANSI C (C89) compiler for the Sparrow09 (Hitachi 6309)
#
#   make                 build cc9
#   make install         install to $(PREFIX)/bin and $(PREFIX)/lib/cc9
#   make test            run the regression tests (needs spechtrun, ROM)
#   make WIN=y           cross compile cc9.exe with MinGW

PREFIX ?= /usr/local
CC     ?= cc
CFLAGS ?= -O2 -g -std=gnu99 -Wall -Wno-unused-function -Wno-format-truncation

ifeq ($(WIN),y)
   CC  = x86_64-w64-mingw32-gcc
   EXT = .exe
endif

OBJS = main.o util.o tokenize.o preprocess.o type.o parse.o codegen.o

cc9$(EXT): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(OBJS): cc9.h

install: cc9$(EXT)
	install -d $(PREFIX)/bin $(PREFIX)/lib/cc9/lib $(PREFIX)/lib/cc9/include
	install cc9$(EXT) $(PREFIX)/bin
	install -m 644 lib/*.c lib/*.as9 $(PREFIX)/lib/cc9/lib
	install -m 644 include/*.h $(PREFIX)/lib/cc9/include

test: cc9$(EXT)
	cd tests && ./run_tests.sh

clean:
	rm -f cc9 cc9.exe $(OBJS)

.PHONY: install test clean
