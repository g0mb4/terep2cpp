#!/bin/sh
# Runs the headless host on the recorded key scripts and compares the
# per-frame CRCs (frame buffer, masked data segment) with the references
# produced by the oracle (tools/oracle, checked against the original asm).
#
#   sh tests/run_tests.sh [path/to/terep2-headless]
cd "$(dirname "$0")/.." || exit 1
BIN=${1:-./terep2-headless}
TMP=${TMPDIR:-/tmp}/terep2-test.$$
fail=0

check() {   # name frames
    "$BIN" data/Asphalt "$2" "$(cat tests/ref/$1.txt)" > "$TMP" || { echo "$1: falhou ao rodar"; fail=1; return; }
    if cmp -s "$TMP" "tests/ref/$1.crc"; then
        echo "$1: ok ($2 quadros)"
    else
        echo "$1: DIFERENTE"
        diff "$TMP" "tests/ref/$1.crc" | head -4
        fail=1
    fi
}

check cenario 600
check fuzz1 3000
rm -f "$TMP"
exit $fail
