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
    if "$CC" "$input" -o "$base.s" 2> "$base.compile.err"; then
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
    if "$CC" "$input" -o "$base.s" 2> "$base.err"; then
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
check 42 'int main() { int a = 42; return a; }'
check 42 'int main() { int a; a = 42; return a; }'
check 42 'int main() { int a; int b; a = 6; b = 7; return a * b; }'
check 49 'int main() { int a = 6; a = a + 1; return a * 7; }'
check 42 'int main() { int a = 1; { int a = 42; return a; } }'
check 42 'int main() { int a = 42; { int a = 41; } return a; }'
check 0 'int main() { int a = 1; int b = 2; }'
check 42 'int main() { int a = 40; { int b = 2; a = a + b; } return a; }'
check 12 'int main() { int a = 4; { int a = 9; a = a + 3; } return a + 8; }'
check 42 'int main() { int a = 21; int b = 3; return (a + b) + (a - b); }'
check 42 'int main() { int a; int b = 4; a = (b = 6) * 7; return a; }'
check 42 'int main() { int a = 12; int b  = -5; return a - b * 6; }'
check 42 'int main() { int a; a = 4 + 2 * 7; a = a * 2 + 6; return a; }'
check 42 'int main() { int a; (a ) = 42; return a; }'
check 42 'int main() { int a = 42; ; a; return a; }'
check 42 'int main() { { int x = 3; } int x = 42; return x; }'
check 42 'int main() { int a=0; int b=1; int c=2; int d=3; int e=4; int f=5; int g=6; int h=7; int i=14; return a+b+c+d+e+f+g+h+i; }'
check 0 'int main() { int a = 42; a = 24; }'
check 42 'int main() { int a = 42; return a; a = 7; }'
check 2 'int main() { int a = 1; { a = 2; int a = 3; a = 4; } return a; }'
check 5 'int main() { int a = 5; { int a = 4; } { int a = 7; a = 9; } return a; }'
check 42 'int main() { int a = 10; int aa = 32; return a + aa; }'
check 42 'int main() { int _a = 20; int A1 = 22; return _a + A1; }'
check 42 'int main() { int a = 0; int b = (a = 40) + 2; return b; }'
check 42 'int main() { int a; return a = 6 * 7; }'
check 42 'int main() { int while_count = 40; int return_value = 2; int _Boolish = 0; return while_count + return_value + _Boolish; }'
check 42 'int main() { return 2147483647 - 2147483605; }'
check 1 'int main() { return 2147483647 / 2147483647; }'
check 1 'int main() { return - 2147483647 + 2147483647 + 1; }'
check 7 'int main() { return 000000000000000000000000000000007; }'
check 42 'int main() { int a = 41; return ++a; }'
check 41 'int main() { int a = 41; return a++; }'
check 42 'int main() { int a = 43; return --a; }'
check 42 'int main() { int a = 42; return a--; }'
check 42 'int main() { int a = 41; a++; return a; }'
check 42 'int main() { int a = 43; --a; return a; }'
check 42 'int main() { int a = 40; ++a; a++; return a; }'
check 1 'int main() { return 1 == 1; }'
check 0 'int main() { return 1 == 2; }'
check 1 'int main() { return 1 != 2; }'
check 0 'int main() { return 1 != 1; }'
check 1 'int main() { return 1 < 2; }'
check 0 'int main() { return 2 < 1; }'
check 1 'int main() { return 1 <= 1; }'
check 0 'int main() { return 2 <= 1; }'
check 1 'int main() { return 2 > 1; }'
check 0 'int main() { return 1 > 2; }'
check 1 'int main() { return 2 >= 2; }'
check 0 'int main() { return 1 >= 2; }'
check 1 'int main() { return 1 < 2 == 1; }'
check 0 'int main() { return 1 < 2 != 1; }'
check 1 'int main() { int a = 41; return a + 1 == 42; }'
check 1 'int main() { return 1 + 1 < 3; }'
check 1 'int main() { return 7 % 3; }'
check 2 'int main() { return 10 % 4; }'
check 255 'int main() { return -7 % 3; }'
check 0 'int main() { return 6 % 2; }'
check 2 'int main() { return 4 * 3 % 5; }'
check 42 'int main() { int a = 40; a += 2; return a; }'
check 42 'int main() { int a = 44; a -= 2; return a; }'
check 42 'int main() { int a = 14; a *= 3; return a; }'
check 42 'int main() { int a = 84; a /= 2; return a; }'
check 42 'int main() { int a = 85; a %= 43; return a; }'
check 42 'int main() { int a = 40; return a += 2; }'
check 42 'int main() { int a = 6; int b = 7; a *= b; return a; }'
check 0 'int main() { int a = 42; a %= 7; return a; }'
check 8 'int main() { return 1 << 3; }'
check 2 'int main() { return 8 >> 2; }'
check 32 'int main() { return 1 << 2 + 3; }'
check 2 'int main() { return 16 >> 3; }'
check 8 'int main() { return 8 >> 1 << 1; }'
check 252 'int main() { return -8 >> 1; }'
check 1 'int main() { return 1 << 0; }'
check 21 'int main() { return 42 >> 1; }'
check 42 'int main() { int a = 42; return a << 2 >> 2; }'
check 40 'int main() { int a = 5; a <<= 3; return a; }'
check 10 'int main() { int a = 40; a >>= 2; return a; }'
check 16 'int main() { int a = 1; return a <<= 4; }'
check 42 'int main() { if (1) return 42; return 24; }'
check 24 'int main() { if (0) return 42; return 24; }'
check 42 'int main() { if (1) return 42; else return 24; }'
check 24 'int main() { if (0) return 42; else return 24; }'
check 42 'int main() { if (1) { return 42; } else { return 24; } }'
check 24 'int main() { if (0) { return 42; } else { return 24; } }'
check 42 'int main() { int a = 1; if (a == 1) return 42; return 0; }'
check 42 'int main() { int a = 0; if (a) return 24; else return 42; }'
check 42 'int main() { if (2 < 1) return 24; else if (1 < 2) return 42; else return 0; }'
check 42 'int main() { int a = 1; if (a == 1) a = 42; return a; }'
check 1 'int main() { int a = 1; if (a == 0) a = 42; return a; }'
check 42 'int main() { int a = 1; int b = 0; if (a) if (b) return 24; else return 42; return 0; }'
check 0 'int main() { int a = 0; int b = 0; if (a) if (b) return 24; else return 42; return 0; }'
check 42 'int main() { if (0) ; else return 42; return 0; }'
check 42 'int main() { int a = 1; if (a++) return 42; return 0; }'
check 1 'int main() { int a = 0; if (a++) return 42; return a; }'
check 42 'int main() { if (1) { int a = 42; return a; } return 0; }'
check 42 'int main() { if (0) return 24; else ; return 42; }'
check 6 'int main() { int i = 3; int sum = 0; while (i) { sum = sum + 2; i = i - 1; } return sum; }'
check 0 'int main() { int i = 0; int sum = 0; while (i) sum = sum + 1; return sum; }'
check 3 'int main() { int i = 0; while (i < 3) i = i + 1; return i; }'
check 0 'int main() { int i = 3; while (i) i = i - 1; return i; }'
check 42 'int main() { int i = 5; while (i) { if (i == 2) return 42; i = i - 1; } return 0; }'
check 4 'int main() { int i = 0; while (i++ < 3) ; return i; }'
check 0 'int main() { while (0) ; return 0; }'
check 9 'int main() { int i = 3; int j; int n = 0; while (i) { j = 3; while (j) { n = n + 1; j = j - 1; } i = i - 1; } return n; }'
check 3 'int main() { int i; for (i = 0; i < 3; i = i + 1) ; return i; }'
check 6 'int main() { int i; int sum = 0; for (i = 0; i < 4; i = i + 1) sum = sum + i; return sum; }'
check 42 'int main() { int i; for (i = 0; i < 10; i = i + 1) if (i == 5) return 42; return 0; }'
check 6 'int main() { int sum = 0; for (int i = 0; i < 4; i = i + 1) sum = sum + i; return sum; }'
check 42 'int main() { for (int i = 0; i < 3; i = i + 1) ; int i = 42; return i; }'
check 42 'int main() { int i = 0; for (;;) { i = i + 1; if (i == 42) return i; } }'
check 1 'int main() { int i = 0; for (; i < 3; i = i + 1) ; return i == 3; }'
check 9 'int main() { int i; int j; int n = 0; for (i = 0; i < 3; i = i + 1) for (j = 0; j < 3; j = j + 1) n = n + 1; return n; }'
check 1 'int main() { return 1 && 1; }'
check 0 'int main() { return 1 && 0; }'
check 0 'int main() { return 0 && 1; }'
check 1 'int main() { return 1 || 0; }'
check 1 'int main() { return 0 || 1; }'
check 0 'int main() { return 0 || 0; }'
check 42 'int main() { int a = 42; 0 && (a = 1); return a; }'
check 42 'int main() { int a = 42; 1 || (a = 1); return a; }'
check 1 'int main() { int a = 0; 1 && (a = 1); return a; }'
check 1 'int main() { int a = 0; 0 || (a = 1); return a; }'
check 1 'int main() { return 2 && 3; }'
check 1 'int main() { int a = 2; return a && a; }'
check 1 'int main() { return 1 || 0 && 0; }'
check 0 'int main() { return (1 || 0) && 0; }'
check 42 'int main() { int a = 1; int b = 2; if (a == 1 && b == 2) return 42; return 0; }'
check 3 'int main() { int i = 0; while (i < 3 && i != 0 || i == 0) i = i + 1; return i; }'
check 1 'int main() { return 1 & 1; }'
check 0 'int main() { return 1 & 2; }'
check 3 'int main() { return 1 | 2; }'
check 0 'int main() { return 0 | 0; }'
check 3 'int main() { return 1 ^ 2; }'
check 0 'int main() { return 5 ^ 5; }'
check 12 'int main() { return 12 & 15; }'
check 15 'int main() { return 12 | 3; }'
check 3 'int main() { return 1 | 2 & 2; }'
check 7 'int main() { return 2 | 4 ^ 3; }'
check 4 'int main() { return 2 & 4 ^ 4; }'
check 1 'int main() { return 1 & 2 == 2; }'
check 1 'int main() { return 5 & 3 && 5 | 3; }'
check 6 'int main() { int a = 4; a |= 2; return a; }'
check 8 'int main() { int a = 12; a &= 10; return a; }'
check 7 'int main() { int a = 3; a ^= 4; return a; }'
check 5 'int main() { int a = 1; return a |= 4; }'
check 24 'int main() { int a = 42; int b = 24; a ^= b; b ^= a; a ^= b; return a; }'
check 1 'int main() { int i = 0; do { i = i + 1; } while (0); return i; }'
check 3 'int main() { int i = 0; do { i = i + 1; } while (i < 3); return i; }'
check 42 'int main() { int i = 0; do { i = i + 1; if (i == 42) return i; } while (1); }'
check 6 'int main() { int i = 3; int sum = 0; do { sum = sum + i; i = i - 1; } while (i); return sum; }'
check 4 'int main() { int i = 0; do { i = i + 1; } while (i++ < 3); return i; }'
check 1 'int main() { int i = 0; do i = i + 1; while (0); return i; }'
check 9 'int main() { int i = 0; int j; int n = 0; do { j = 0; do { n = n + 1; j = j + 1; } while (j < 3); i = i + 1; } while (i < 3); return n; }'
check 42 $'int main() { // comment\n return 42; }'
check 42 'int main() { /* comment */ return 42; }'
check 42 $'int main() { /* multi\nline\ncomment */ return 42; }'
check 42 'int main() { return /* inline */ 42; }'
check 7 'int main() { /* a */ return /* b */ 3 /* c */ + /* d */ 4; }'
check 0 $'int main() { return 0; } // trailing comment'

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
check_error trailing-identifier 'int main() { return 42; garbage }' "1:25: error: undeclared identifier"
check_error trailing-token 'int main() { return 42; @ }' "1:25: error: expect expr"
check_error trailing-after-bare-return 'int main() { return; garbage }' '1:20: error: expect return expr'
check_error empty-input '' "1:1: error: expect 'int'"
check_error whitespace-only $' \t\n' "2:1: error: expect 'int'"
check_error undeclared-read 'int main() { return a; }' '1:21: error: undeclared identifier'
check_error undeclared-assign 'int main() { a = 3; return 0; }' '1:14: error: undeclared identifier'
check_error use-before-declared 'int main() { a=3; int a; return a; }' '1:14: error: undeclared identifier'
check_error duplicate-local 'int main() { int a; int a; return 0; }' '1:25: error: duplicate local variable'
check_error duplicate-init 'int main() { int a = 2; int a = 3; return a; }' '1:29: error: duplicate local variable'
check_error out-of-scope 'int main() { { int a = 42; } return a; }' '1:37: error: undeclared identifier'
check_error sibling-out-of-scope 'int main() { { int a = 9; } { return a; } }' '1:38: error: undeclared identifier'
check_error invalid-lvalue-literal 'int main() { 1=3; return 0; }' '1:15: error: invalid assignment target'
check_error invalid-lvalue-sum 'int main() { int a; (a + 1) = 3; return 0; }' '1:29: error: invalid assignment target'
check_error invalid-lvalue-unary 'int main() { int a; -a = 3; return 0; }' '1:24: error: invalid assignment target'
check_error bad-assign-rhs 'int main() { int a; a =; return 0; }' '1:24: error: expect expr'
check_error bad-assign-rhs-eof 'int main() { int a; a=' '1:23: error: expect expr'
check_error missing-decl-name 'int main() { int = 3; return 0; }' '1:18: error: expect variable name'
check_error missing-decl-semicolon 'int main() { int a = 3 return a; }' "1:24: error: expect ';'"
check_error missing-decl-init 'int main() { int a =; return 0; }' '1:21: error: expect expr'
check_error nested-invalid 'int main() { int a; { int a; int a; } }' '1:34: error: duplicate local variable'
check_error bad-assignment-nested 'int main() { int a; a = 1 = 2; }' '1:27: error: invalid assignment target'
check_error missing-expr-semicolon 'int main() { int a; a = 3 return a; }' "1:27: error: expect ';'"
check_error missing-identifier-after-int 'int main() { int; return 0; }' '1:17: error: expect variable name'
check_error declare-keyword 'int main() { int return; return 0; }' '1:18: error: expect variable name'
check_error use-post-scope-shadow 'int main() { { int x = 1; } x = 42; return 0; }' '1:29: error: undeclared identifier'
check_error redecl-after-nested 'int main() { int a; { int a; } int a; }' '1:36: error: duplicate local variable'
check_error unsupported-comma-decl 'int main() { int a, b; return 0; }' "1:19: error: expect ';'"
check 42 'int main() { int a = 42; a == 3; return a; }'
for kw in \
    _Alignas _Alignof _Atomic _Bool _Complex _Generic _Imaginary \
	     _Noreturn _Static_assert _Thread_local \
	     auto break case char const continue default do double else enum extern \
	     float for goto if inline long register restrict short signed sizeof \
	     static struct switch typedef union unsigned void volatile while; do
    check_error "reserved-kw-$kw" \
		"int main() { int $kw; return 0; }" \
		'1:18: error: expect variable name'
done
# TODO: Allow different type of integer literals
check_error literal-over-int32 'int main() { return 2147483648; }' \
	    '1:21: error: integer literal out of range for 32-bit int'
check_error invalid-increment 'int main() { return ++1; }' \
	    '1:21: error: invalid increment target'
check_error invalid-increment 'int main() { return 1++; }' \
	    '1:22: error: invalid increment target'
check_error invalid-increment 'int main() { int a; return ++(a+1); }' \
	    '1:28: error: invalid increment target'
check_error invalid-decrement 'int main() { return --1; }' \
	    '1:21: error: invalid decrement target'
check_error invalid-postfix-chain 'int main() { int a; return (a++)++; }' \
	    '1:33: error: invalid increment target'
check_error undeclared-postfix 'int main() { a++; return 0; }' \
	    '1:14: error: undeclared identifier'
check_error missing-compare-rhs 'int main() { return 1 <; }' \
	    '1:24: error: expect expr'
check_error missing-eq-rhs 'int main() { return 1 ==; }' \
	    '1:25: error: expect expr'
check_error compound-assign-literal 'int main() { 1 += 2; return 0; }' \
	    '1:16: error: invalid assignment target'
check_error compound-assign-sum 'int main() { int a; a + 1 += 2; }' \
	    '1:27: error: invalid assignment target'
check_error missing-shift-rhs 'int main() { return 1 <<; }' \
	    '1:25: error: expect expr'
check_error shift-assign-literal 'int main() { 1 <<= 2; return 0; }' \
	    '1:16: error: invalid assignment target'
check_error missing-if-paren 'int main() { if 1; return 0; }' \
	    "1:17: error: expect '('"
check_error missing-if-cond 'int main() { if () return 2; }' \
	    '1:18: error: expect expr'
check_error missing-if-rparen 'int main() { if (1 return 2; }' \
	    "1:20: error: expect ')'"
check_error missing-if-body 'int main() { if (1) }' \
	    '1:21: error: expect expr'
check_error missing-while-paren 'int main() { while 1; return 0; }' \
	    "1:20: error: expect '('"
check_error missing-while-cond 'int main() { while () return 0; }' \
	    '1:21: error: expect expr'
check_error missing-while-rparen 'int main() { while (1 return 0; }' \
	    "1:23: error: expect ')'"
check_error missing-while-body 'int main() { while (1) }' \
	    '1:24: error: expect expr'
check_error missing-for-paren 'int main() { for 1; return 0; }' \
	    "1:18: error: expect '('"
check_error missing-for-semi 'int main() { for (1) return 0; }' \
	    "1:20: error: expect ';'"
check_error missing-for-rparen 'int main() { for (1; 1; 1 return 0; }' \
	    "1:27: error: expect ')'"
check_error logical-and-operand 'int main() { return && 1; }' \
	    '1:21: error: expect expr'
check_error missing-logical-and-rhs 'int main() { return 1 &&; }' \
	    '1:25: error: expect expr'
check_error missing-logical-or-rhs 'int main() { return 1 ||; }' \
	    '1:25: error: expect expr'
check_error missing-bitand-rhs 'int main() { return 1 &; }' \
	    '1:24: error: expect expr'
check_error missing-bitor-rhs 'int main() { return 1 |; }' \
	    '1:24: error: expect expr'
check_error missing-bitxor-rhs 'int main() { return 1 ^; }' \
	    '1:24: error: expect expr'
check_error bitand-operand 'int main() { return & 1; }' \
	    '1:21: error: expect expr'
check_error bitand-assign-literal 'int main() { 1 &= 2; return 0; }' \
	    '1:16: error: invalid assignment target'
check_error missing-do-while 'int main() { do return 0; }' \
	    "1:27: error: expect 'while'"
check_error missing-do-cond 'int main() { do return 0; while (); }' \
	    '1:34: error: expect expr'
check_error missing-do-rparen 'int main() { do return 0; while (1; }' \
	    "1:35: error: expect ')'"
check_error missing-do-semi 'int main() { do return 0; while (1) }' \
	    "1:37: error: expect ';'"
check_error missing-do-body 'int main() { do }' \
	    '1:17: error: expect expr'
check_error unterminated-comment 'int main() { /* comment' \
	    '1:24: error: unterminated comment'

printf 'All %d tests passed (%d execution, %d rejection).\n' "$((passed + rejected))" "$passed" "$rejected"
