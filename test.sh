#!/usr/bin/env bash

set -e

GCC="gcc"
CC="./cc"
INPUT_DIR="./tests/input"
OUTPUT_DIR="./tests/output"

function check() {
    base=$(basename "$1" .c)
    "$CC" "$INPUT_DIR/$1" > "/tmp/$base.s"
    "$GCC" "/tmp/$base.s" -o "/tmp/$base.out"

    set +e
    "/tmp/$base.out"
    actual_code=$?
    set -e

    if [[ "$actual_code" -eq "$2" ]]; then
	echo -e "[PASS] $base (exit code $actual_code)"
    else
	echo -e "[FAIL] $base (exit code $actual_code, expected $2)"
    fi
}

check 1.c 0
check 2.c 42
check 3.c 3
check 4.c 4
check 5.c 3
check 6.c 2
check 7.c 9
check 8.c 1
check 9.c 255
check 10.c 0
check 11.c 245
check 12.c 1
