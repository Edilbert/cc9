#!/bin/sh
# Build sparrow.rom + sparrow.sym from a sparrow09 checkout for the tests.
# usage: build_rom.sh /path/to/sparrow09 [outdir]
#
# The ROM sources (Nov 2026) do not assemble with the current BS9:
#  - bios.as9 defines K_FLDA ... K_LOAD as labels, api.as9 as constants
#  - FCB is used with 16 bit values (old BYTE semantics), e.g. basic.as9
# This script works on a copy and applies both changes there.
set -e
SRC=${1:?usage: build_rom.sh /path/to/sparrow09 [outdir]}
OUT=${2:-.}
BS9=${BS9:-bs9}
T=$(mktemp -d)
cp -r "$SRC/software/ROM/." "$T/"
cd "$T"
sed -i 's/^K_\(FLDA\|FLDD\|FSTA\|FSTD\|SAVE\|LOAD\) /        /' bios.as9
sed -i 's/\([ \t]\)\(FCB\|fcb\)\([ \t]\)/\1BYTE\3/g' *.as9
$BS9 -q -l 0xFF sparrow.as9
cd - > /dev/null
cp "$T/sparrow.rom" "$T/sparrow.sym" "$OUT/"
rm -rf "$T"
echo "sparrow.rom and sparrow.sym written to $OUT"
