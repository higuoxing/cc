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

check 0 'int main() {}'
check 42 'int main() { return 42; }'
check 3  'int main() { return 1+2; }'
check 4 'int main() { return 1 +        1 +1+1; }'
check 3 'int main() { return (1+2); }'
check 2 'int main() { return 1 * 2; }'
check 9 'int main() { return 3 * (1+2); }'
check 1 'int main() { return (1+2) / 3; }'
check 255 'int main() { return -1; }'
check 0 'int main() { return !1; }'
check 245 'int main() { return ~10; }'
check 1 'int main() { return !!10; }'
check 42 'int main() { return (((42))); }'
check 42 'int main() { return -(-6) * 7; }'
check 42 'int main() { return 2 * -3 + 48; }'
check 42 $'int main() { return (\n6 * 7\n)\n;\n }'

check_error missing-plus-rhs 'int main() { return 1 +; }' '1:24: error: expect expr'
check_error missing-sub-rhs 'int main() { return 1 -; }' '1:24: error: expect expr'
check_error missing-mul-rhs 'int main() { return 1 *; }' '1:24: error: expect expr'
check_error missing-div-rhs 'int main() { return 1 /; }' '1:24: error: expect expr'
check_error missing-neg-operand 'int main() { return -; }' '1:22: error: expect expr'
check_error missing-not-operand 'int main() { return !; }' '1:22: error: expect expr'
check_error missing-bitnot-operand 'int main() { return ~; }' '1:22: error: expect expr'
check_error missing-unary-chain 'int main() { return !~-; }' '1:24: error: expect expr'
check_error empty-parens 'int main() { return (); }' '1:22: error: expect expr'
check_error nested-empty-parens 'int main() { return (()); }' '1:23: error: expect expr'
check_error empty-rhs 'int main() { return 1 + (); }' '1:26: error: expect expr'
check_error empty-lhs 'int main() { return () + 1; }' '1:22: error: expect expr'
check_error unexpected-mul 'int main() { return * 2; }' '1:21: error: expect expr'
check_error double-operator 'int main() { return 1 + * 2; }' '1:25: error: expect expr'
check_error unknown-operand 'int main() { return @; }' '1:21: error: expect expr'
check_error keyword-operand 'int main() { return return; }' '1:21: error: expect expr'
check_error unexpected-close 'int main() { return ); }' '1:21: error: expect expr'
check_error missing-rhs-eof 'int main() { return 1 + }' '1:25: error: expect expr'
check_error missing-neg-eof 'int main() { return - }' '1:23: error: expect expr'
check_error missing-expr-eof 'int main() { return }' '1:21: error: expect expr'
check_error missing-rparen 'int main() { return (1 + 2; }' "1:27: error: expect ')'"
check_error missing-rparen-eof 'int main() { return (1 + 2 }' "1:28: error: expect ')'"
check_error missing-semicolon 'int main() { return 42 }' "1:24: error: expect ';'"
check_error extra-rparen 'int main() { return (1+2)); }' "1:26: error: expect ';'"
check_error adjacent-literals 'int main() { return 1 2; }' "1:23: error: expect ';'"
check_error trailing-identifier 'int main() { return 42; garbage }' "1:25: error: unexpected token after statement"
check_error trailing-token 'int main() { return 42; @ }' "1:25: error: unexpected token after statement"
check_error trailing-after-bare-return 'int main() { return; garbage }' '1:20: error: expect return expr'
check_error empty-input '' "1:1: error: expect 'int'"
check_error whitespace-only $' \t\n' "2:1: error: expect 'int'"

printf 'All %d tests passed (%d execution, %d rejection).\n' "$((passed + rejected))" "$passed" "$rejected"
