# spechtrun: headless test runner built from the unchanged Specht source
#
#   make SPECHT=/path/to/sparrow09/software/specht/specht.c
#
SPECHT ?= ../../../sparrow09/software/specht/specht.c
CC ?= cc

spechtrun: spechtrun.c stub/SDL3/SDL.h $(SPECHT)
	$(CC) -O2 -std=gnu99 -w -I stub -DSDL3 -DSPECHT_C='"$(abspath $(SPECHT))"' -o $@ spechtrun.c

clean:
	rm -f spechtrun
