#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

GCC="${GCC:-gcc}"
CC="${TEST_CC:-$SCRIPT_DIR/cc}"
TEST_KEEP_TMP="${TEST_KEEP_TMP:-0}"

passed=0
rejected=0

WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/test-cc.XXXXXX")
function cleanup() {
    if [[ "$TEST_KEEP_TMP" == 1 ]]; then
	printf 'Temporary files kept in %s\n' "$WORK_DIR" >&2
    else
	rm -rf -- "$WORK_DIR"
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

function fail() {
    printf '[FAIL] %s\n' "$*" >&2
    exit 1
}

function check() {
    local expected=$1
    local source=$2
    local name=${3:-test$((passed + 1))}
    local base="$WORK_DIR/execute-$((passed + 1))"
    local input="$base.c"
    local actual_code

    printf '%s' "$source" > "$input"
    if "$CC" "$input" > "$base.s" 2> "$base.compile.err"; then
	:
    else
	actual_code=$?
	cat "$base.compile.err" >&2
	fail "$name: compiler exited with status $actual_code"
    fi

    # Don't allow a stale executable to hide a linker that produced no output.
    rm -f -- "$base.out"
    if "$GCC" "$base.s" -o "$base.out" > "$base.link.log" 2>&1; then
	:
    else
	actual_code=$?
	cat "$base.link.log" >&2
	fail "$name: assembler/linker exited with status $actual_code"
    fi
    [[ -f "$base.out" && -x "$base.out" ]] || fail "$name: linker produced no executable"

    if "$base.out" > "$base.stdout" 2> "$base.stderr"; then
	actual_code=0
    else
	actual_code=$?
    fi
    if [[ "$actual_code" -ne "$expected" ]]; then
	cat "$base.stdout" "$base.stderr" >&2
	printf 'Source: %q\n' "$source" >&2
	fail "$name (exit code $actual_code, expected $expected)"
    fi

    passed=$((passed + 1))
    printf '[PASS] %s (exit code %s)\n' "$name" "$actual_code"
}

function check_error() {
    local name=$1
    local source=$2
    local diag=$3
    local base="$WORK_DIR/reject-$((rejected + 1))"
    local input="$base.c"
    local actual_code

    printf '%s' "$source" > "$input"
    if "$CC" "$input" > "$base.s" 2> "$base.err"; then
	actual_code=0
    else
	actual_code=$?
    fi

    # Crash, missing compiler, or accidental acceptance is not a rejection.
    if [[ $actual_code -ne 1 ]]; then
	cat "$base.err" >&2
	fail "$name: compiler exited with status $actual_code, expected 1"
    fi

    printf '%s:%s\n' "$input" "$diag" > "$base.expected.err"
    if ! cmp -s "$base.expected.err" "$base.err"; then
	printf 'Expected diagnostic:\n' >&2
	cat "$base.expected.err" >&2
	printf 'Actual diagnostic:\n' >&2
	cat "$base.err" >&2
	fail "$name: unexpected diagnostic"
    fi
    if [[ -s "$base.s" ]]; then
	fail "$name: emitted assembly for invalid input"
    fi

    rejected=$((rejected + 1))
    printf '[PASS] %s (rejected with expected diagnostic)\n' "$name"
}

check 0 'return;'
check 42 'return 42;'
check 3  'return 1+2;'
check 4 'return 1 +        1 +1+1;'
check 3 'return (1+2);'
check 2 'return 1 * 2;'
check 9 'return 3 * (1+2);'
check 1 'return (1+2) / 3;'
check 255 'return -1;'
check 0 'return !1;'
check 245 'return ~10;'
check 1 'return !!10;'
check 42 'return (((42)));'
check 42 'return -(-6) * 7;'
check 42 'return 2 * -3 + 48;'
check 42 $'return (\n6 * 7\n)\n;\n'

check_error missing-plus-rhs 'return 1 +;' '1:11: error: expect expr'
check_error missing-sub-rhs 'return 1 -;' '1:11: error: expect expr'
check_error missing-mul-rhs 'return 1 *;' '1:11: error: expect expr'
check_error missing-div-rhs 'return 1 /;' '1:11: error: expect expr'
check_error missing-neg-operand 'return -;' '1:9: error: expect expr'
check_error missing-not-operand 'return !;' '1:9: error: expect expr'
check_error missing-bitnot-operand 'return ~;' '1:9: error: expect expr'
check_error missing-unary-chain 'return !~-;' '1:11: error: expect expr'
check_error empty-parens 'return ();' '1:9: error: expect expr'
check_error nested-empty-parens 'return (());' '1:10: error: expect expr'
check_error empty-rhs 'return 1 + ();' '1:13: error: expect expr'
check_error empty-lhs 'return () + 1;' '1:9: error: expect expr'
check_error unexpected-mul 'return * 2;' '1:8: error: expect expr'
check_error double-operator 'return 1 + * 2;' '1:12: error: expect expr'
check_error unknown-operand 'return @;' '1:8: error: expect expr'
check_error keyword-operand 'return return;' '1:8: error: expect expr'
check_error unexpected-close 'return );' '1:8: error: expect expr'
check_error missing-rhs-eof 'return 1 +' '1:11: error: expect expr'
check_error missing-neg-eof 'return -' '1:9: error: expect expr'
check_error missing-expr-eof 'return' '1:7: error: expect expr'
check_error missing-rparen 'return (1 + 2;' "1:14: error: expect ')'"
check_error missing-rparen-eof 'return (1 + 2' "1:14: error: expect ')'"
check_error missing-semicolon 'return 42' "1:10: error: expect ';'"
check_error extra-rparen 'return (1+2));' "1:13: error: expect ';'"
check_error adjacent-literals 'return 1 2;' "1:10: error: expect ';'"
check_error trailing-identifier 'return 42; garbage' "1:12: error: unexpected token after statement"
check_error trailing-token 'return 42; @' "1:12: error: unexpected token after statement"
check_error second-statement 'return 1; return 2;' "1:11: error: unexpected token after statement"
check_error trailing-after-bare-return 'return; garbage' '1:9: error: unexpected token after statement'
check_error empty-input '' '1:1: error: failed to parse statement'
check_error whitespace-only $' \t\n' '2:1: error: failed to parse statement'
check_error missing-statement '42;' '1:1: error: failed to parse statement'
check_error trailing-multiline $'return 42;\nreturn 1;' '2:1: error: unexpected token after statement'
check_error missing-rhs-lf $'return 1 +\n;' '2:1: error: expect expr'

printf 'All %d tests passed (%d execution, %d rejection).\n' "$((passed + rejected))" "$passed" "$rejected"
