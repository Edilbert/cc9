#!/bin/sh
# cc9 regression tests: compile with cc9, run in Specht (spechtrun),
# compare the output with the expected output.
#
# Environment:
#   CC9          cc9 executable           (default: ../cc9)
#   SPECHTRUN    spechtrun executable     (default: ../tools/spechtrun/spechtrun)
#   SPARROW_ROM  sparrow.rom              (default: ./sparrow.rom)
#   SPARROW_SYM  sparrow.sym              (default: ./sparrow.sym)
#   CTS          c-testsuite checkout     (optional, runs the c89 tests)
cd "$(dirname "$0")"
CC9=${CC9:-../cc9}
RUN=${SPECHTRUN:-../tools/spechtrun/spechtrun}
ROM=${SPARROW_ROM:-sparrow.rom}
SYM=${SPARROW_SYM:-sparrow.sym}
T=$(mktemp -d)
pass=0; fail=0

run() {  # file.x [input]
   if [ -n "$2" ]; then
      $RUN -r $ROM -y $SYM -n 3000000000 "$1" < "$2"
   else
      $RUN -r $ROM -y $SYM -n 3000000000 "$1" < /dev/null
   fi
}

for t in c/*.c; do
   n=$(basename $t .c)
   in=""; [ -f c/$n.in ] && in=c/$n.in
   if $CC9 -w -o $T/$n.x $t && run $T/$n.x $in > $T/$n.out 2>&1 && cmp -s $T/$n.out c/$n.expected; then
      pass=$((pass+1))
   else
      fail=$((fail+1)); echo "FAIL $n"
   fi
done

# soft float: random IEEE test vectors (needs python3 + numpy)
if python3 -c "import numpy" 2>/dev/null; then
   for seed in 1 2 3 4 5; do
      python3 fp/gen.py $seed 100 > $T/v$seed.c
      if $CC9 -w -o $T/v$seed.x $T/v$seed.c && run $T/v$seed.x | grep -q "bad=0"; then
         pass=$((pass+1))
      else
         fail=$((fail+1)); echo "FAIL float vectors seed $seed"
      fi
   done
fi

if [ -n "$CTS" ]; then
   for t in $CTS/tests/single-exec/*.c; do
      grep -qw c89 $t.tags || continue
      n=$(basename $t .c)
      if $CC9 -w -o $T/cts$n.x $t 2>/dev/null && run $T/cts$n.x > $T/cts$n.out 2>&1 &&
         cmp -s $T/cts$n.out $t.expected; then
         pass=$((pass+1))
      else
         echo "fail c-testsuite $n"
      fi
   done
fi

rm -rf $T
echo "passed: $pass  failed: $fail"
[ $fail -eq 0 ]
