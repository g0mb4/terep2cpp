#!/bin/sh
# Build the reference (C++ previous_effort) host, optionally linked with the
# new code so subsystems can be swapped with --new.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE/../build}
mkdir -p "$OUT"
python3 "$HERE/gen_dataseg.py" >/dev/null
CXXFLAGS="-std=gnu++17 -O2 -g -fwrapv -ffp-contract=off -Wall -Wno-address-of-packed-member -Wno-unused-result"
g++ -std=gnu++20 -O1 -g -c "$HERE/oracle/maincode.cpp" -o "$OUT/maincode.o"
g++ -std=gnu++20 -O1 -g -c "$HERE/oracle/host.cpp" -o "$OUT/host.o"
g++ $CXXFLAGS -c "$HERE/oracle/bridge.cpp" -o "$OUT/bridge.o"
OBJS=""
for f in "$HERE"/../src/*.cpp; do
    o="$OUT/new_$(basename "$f" .cpp).o"
    g++ $CXXFLAGS -c "$f" -o "$o"
    OBJS="$OBJS $o"
done
g++ -o "$OUT/oracle" "$OUT/maincode.o" "$OUT/host.o" "$OUT/bridge.o" $OBJS -lz -lm
echo "built $OUT/oracle"
