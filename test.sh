#!/usr/bin/env bash

set -e

GCC="gcc"
CC="./cc"
INPUT_DIR="./tests/input"
OUTPUT_DIR="./tests/output"

passed=0
failed=0
total=0

for inp in "$INPUT_DIR"/*.c; do
    [[ -e $inp ]] || { echo "No .c files found"; exit 0; }

    base=$(basename "$inp" .c)
    "$CC" "$inp" > "/tmp/$base.s"
    "$GCC" "/tmp/$base.s" -o "/tmp/$base.out"

    set +e
    "/tmp/$base.out"
    actual_code=$?
    set -e

    if [[ "$actual_code" -eq "$expected_code" ]]; then
	echo -e "  [PASS] $base (exit code $actual_code)"
	passed=$((passed + 1))
    else
	echo -e "  [FAIL] $base (exit code $actual_code)"
	failed=$((failed + 1))
    fi
done

echo "-------------------------"
echo -e "Result: $passed passed, $failed failed, total $total"

if [[ "$failed" != 0 ]]; then
    exit 1
fi
