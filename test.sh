#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

CC="${CC:-gcc}"
TEST_CC="${TEST_CC:-$SCRIPT_DIR/cc}"
TEST_KEEP_TMP="${TEST_KEEP_TMP:-0}"

compare=0
for arg in "$@"; do
    case "$arg" in
        --compare) compare=1 ;;
        -h|--help)
            printf 'Usage: %s [--compare]\nCompare exit codes with CC (default: gcc) when --compare is specified.\n' "$0"
            exit 0
            ;;
        *)
            printf 'Unknown option: %s\nUsage: %s [--compare]\n' "$arg" "$0" >&2
            exit 2
            ;;
    esac
done

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
    local reference_code

    printf '%s' "$source" > "$input"
    if "$TEST_CC" "$input" -o "$base.s" 2> "$base.compile.err"; then
	:
    else
	actual_code=$?
	cat "$base.compile.err" >&2
	fail "$name: compiler exited with status $actual_code"
    fi

    # Don't allow a stale executable to hide a linker that produced no output.
    rm -f -- "$base.out"
    if "$CC" "$base.s" -o "$base.out" > "$base.link.log" 2>&1; then
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

    if ((compare)); then
        if "$CC" -std=c11 -O0 "$input" -o "$base.reference.out" > "$base.reference.compile.log" 2>&1; then
            :
        else
            reference_code=$?
            cat "$base.reference.compile.log" >&2
            printf 'Source: %q\n' "$source" >&2
            fail "$name: reference compiler ($CC) exited with status $reference_code"
        fi
        [[ -f "$base.reference.out" && -x "$base.reference.out" ]] || fail "$name: reference compiler produced no executable"

        if "$base.reference.out" > "$base.reference.stdout" 2> "$base.reference.stderr"; then
            reference_code=0
        else
            reference_code=$?
        fi
        if [[ "$actual_code" -ne "$reference_code" ]]; then
            cat "$base.stdout" "$base.stderr" "$base.reference.stdout" "$base.reference.stderr" >&2
            printf 'Source: %q\n' "$source" >&2
            fail "$name: $TEST_CC exit code $actual_code, $CC exit code $reference_code, expected $expected"
        fi
    fi

    passed=$((passed + 1))
    if ((compare)); then
        printf '[PASS] %s (exit code %s, matches %s)\n' "$name" "$actual_code" "$CC"
    else
        printf '[PASS] %s (exit code %s)\n' "$name" "$actual_code"
    fi
}

function check_error() {
    local name=$1
    local source=$2
    local diag=$3
    local base="$WORK_DIR/reject-$((rejected + 1))"
    local input="$base.c"
    local actual_code

    printf '%s' "$source" > "$input"
    if "$TEST_CC" "$input" -o "$base.s" 2> "$base.err"; then
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
check 42 'int main() { int longer = 20; int long_ = 21; int long1 = 1; return longer + long_ + long1; }' long-keyword-prefix-identifiers
check 42 'int main() { int shorter = 20; int short_ = 21; int short1 = 1; return shorter + short_ + short1; }' short-keyword-prefix-identifiers
check 42 'int main() { int a = 0; int b = (a = 40) + 2; return b; }'
check 42 'int main() { int a; return a = 6 * 7; }'
check 42 'int main() { int while_count = 40; int return_value = 2; int _Boolish = 0; return while_count + return_value + _Boolish; }'
check 42 'int main() { return 2147483647 - 2147483605; }'
check 1 'int main() { return 2147483647 / 2147483647; }'
check 1 'int main() { return - 2147483647 + 2147483647 + 1; }'
check 7 'int main() { return 000000000000000000000000000000007; }'
check 0 'int main() { return 0; }' octal-zero
check 0 'int main() { return 000; }' octal-zeros
check 7 'int main() { return 07; }' octal-seven
check 8 'int main() { return 010; }' octal-ten
check 42 'int main() { return 052; }' octal-fifty-two
# Integer suffix spelling and token boundaries; literal typing follows later.
for suffix in U u L l UL Ul uL ul LU Lu lU lu; do
    check 42 "int main(void) { return 42$suffix; }" "integer-suffix-decimal-$suffix"
    check 42 "int main(void) { return 052$suffix; }" "integer-suffix-octal-$suffix"
done
check 42 'int main(void) { return 1U+051L; }' integer-suffix-before-operator
check 0 'int main(void) { return 0uL; }' integer-suffix-zero
check_error integer-suffix-duplicate-unsigned 'int main(void) { return 42UU; }' '1:25: error: invalid integer suffix'
check_error integer-suffix-duplicate-unsigned-case 'int main(void) { return 42Uu; }' '1:25: error: invalid integer suffix'
check_error integer-suffix-duplicate-unsigned-after-long 'int main(void) { return 42ULU; }' '1:25: error: invalid integer suffix'
check_error integer-suffix-long-long 'int main(void) { return 42LL; }' '1:25: error: long long integer literals not supported'
check_error integer-suffix-long-long-lowercase 'int main(void) { return 42ll; }' '1:25: error: long long integer literals not supported'
check_error integer-suffix-long-long-mixed-case 'int main(void) { return 42lL; }' '1:25: error: long long integer literals not supported'
check_error integer-suffix-unsigned-long-long 'int main(void) { return 42uLL; }' '1:25: error: long long integer literals not supported'
check_error integer-suffix-trailing-letter 'int main(void) { return 42ULx; }' '1:25: error: invalid integer suffix'
check_error integer-suffix-trailing-digit 'int main(void) { return 42U2; }' '1:25: error: invalid integer suffix'
check_error integer-suffix-trailing-underscore 'int main(void) { return 42L_name; }' '1:25: error: invalid integer suffix'
check_error integer-suffix-invalid-octal 'int main(void) { return 08U; }' '1:25: error: invalid digit in octal integer literal'

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
check 42 'int main() { int i = 0; while (1) { i = i + 1; if (i == 42) break; } return i; }'
check 0 'int main() { while (1) break; return 0; }'
check 5 'int main() { int i = 0; int n = 0; while (i < 10) { i = i + 1; if (i % 2) continue; n = n + 1; } return n; }'
check 5 'int main() { int i; int n = 0; for (i = 0; i < 10; i = i + 1) { if (i % 2) continue; n = n + 1; } return n; }'
check 5 'int main() { int i = 0; int n = 0; do { i = i + 1; if (i % 2) continue; n = n + 1; } while (i < 10); return n; }'
check 3 'int main() { int i = 0; do { i = i + 1; if (i == 3) break; } while (1); return i; }'
check 9 'int main() { int i; int j; int n = 0; for (i = 0; i < 3; i = i + 1) for (j = 0; j < 10; j = j + 1) { if (j == 3) break; n = n + 1; } return n; }'
check 6 'int main() { int i; int j; int n = 0; for (i = 0; i < 3; i = i + 1) for (j = 0; j < 3; j = j + 1) { if (j == 1) continue; n = n + 1; } return n; }'
check 42 'int main() { goto end; return 24; end: return 42; }'
check 6 'int main() { int i = 0; int sum = 0; loop: sum = sum + i; i = i + 1; if (i <= 3) goto loop; return sum; }'
check 42 'int main() { int i = 0; L: ; i = i + 1; if (i < 2) goto L; return 42; }'
check 42 'int main() { int i = 0; while (i < 2) { i = i + 1; if (i == 2) goto done; } return 0; done: return 42; }'
check 42 'int main() { if (1) goto L; return 24; L: return 42; }'
check 42 'int main() { int x = 2; switch (x) { case 1: return 24; case 2: return 42; case 3: return 0; } return 0; }'
check 42 'int main() { int x = 9; switch (x) { case 1: return 24; default: return 42; } }'
check 7 'int main() { int x = 9; switch (x) { case 1: x = 1; case 2: x = 2; } return 7; }'
check 42 'int main() { int x = 2; switch (x) { case 1: x = 24; break; case 2: x = 42; break; default: x = 0; } return x; }'
check 42 'int main() { int x = 1; int n = 0; switch (x) { case 1: n = n + 40; case 2: n = n + 2; } return n; }'
check 42 'int main() { int x = 5; switch (x) { case 2 + 3: return 42; default: return 0; } }'
check 42 'int main() { int x = -1; switch (x) { case -1: return 42; default: return 0; } }'
check 42 'int main() { int i; for (i = 0; i < 3; i = i + 1) { switch (i) { case 0: break; case 1: break; case 2: return 42; } } return 0; }'
check 42 'int main() { int a = 1; int b = 2; switch (a) { case 1: switch (b) { case 2: return 42; } } return 0; }'
check 42 'int main() { int x = 2; switch (x) { { case 2: return 42; } } return 0; }'
check 42 'int foo() { return 24; } int main() { return 42; }'
check 42 'int main() { return 42; } int foo() { return 24; }'
check 42 'int foo() { L: ; return 24; } int main() { L: ; return 42; }'
check 42 'int foo() { int x = 0; while (x < 3) x = x + 1; switch (x) { case 3: return 42; } return 0; } int main() { return 42; }'
check 42 'int foo() { return 42; } int main() { return foo(); }'
check 42 'int foo() { return 40; } int main() { return foo() + 2; }'
check 42 'int foo() { return 42; } int main() { foo(); return 42; }'
check 42 'int foo() { return 21; } int main() { return foo() + foo(); }'
check 42 'int foo() { return 42; } int bar() { return foo(); } int main() { return bar(); }'
check 3 'int one() { return 1; } int main() { return one() + one() + one(); }'
check 127 'int foo() { return 40; } int main() { return 1 + (2 + foo()) * 3; }'
check 42 'int add(int a, int b) { return a + b; } int main() { return add(40, 2); }'
check 42 'int add1(int a) { return a + 1; } int main() { return add1(41); }'
check 120 'int fact(int n) { if (n == 0) return 1; return n * fact(n - 1); } int main() { return fact(5); }'
check 55 'int fib(int n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); } int main() { return fib(10); }'
check 21 'int sum6(int a, int b, int c, int d, int e, int f) { return a + b + c + d + e + f; } int main() { return sum6(1, 2, 3, 4, 5, 6); }'
check 42 'int pick(int a, int b) { return b; } int main() { int x = 42; return pick(0, x); }'
check 42 'int foo(int a); int foo(int a) { return a + 41; } int main() { return foo(1); }'
check 42 'int foo(int a) { return a + 41; } int foo(int a); int main() { return foo(1); }'
check 42 'int foo(int a); int foo(int a); int main() { return foo(1); } int foo(int a) { return a + 41; }'
check 42 'int is_even(int n); int is_odd(int n) { if (n == 0) return 0; return is_even(n - 1); } int is_even(int n) { if (n == 0) return 1; return is_odd(n - 1); } int main() { if (is_even(10)) return 42; return 0; }'
check 42 'int main() { return 1 ? 42 : 24; }'
check 24 'int main() { return 0 ? 42 : 24; }'
check 42 'int main() { return 5 ? 42 : 24; }'
check 42 'int main() { int a = 42; 1 ? a : (a = 0); return a; }'
check 42 'int main() { int a = 42; 0 ? (a = 0) : 1; return a; }'
check 42 'int main() { return 0 ? 24 : 1 ? 42 : 0; }'
check 42 'int main() { return 1 ? 0 ? 24 : 42 : 0; }'
check 84 'int main() { return (1 ? 2 : 3) * 42; }'
check 42 'int main() { if (1 ? 1 : 0) return 42; return 0; }'
check 42 'int main() { int a = 1; return a++ ? 42 : 24; }'
check 42 'int one() { return 1; } int main() { return one() ? 42 : 24; }'
check 42 'int main() { int x = 42; int *p; p = &x; return *p; }'
check 42 'int main() { int x = 42; int *p = &x; return *p; }' pointer-init-address
check 42 'int main() { int x = 42; int *p = &x; int *q = p; return *q; }' pointer-init-copy
check 7 'int main() { int x = 42; int *p = &x; int **pp = &p; **pp = 7; return x; }' pointer-init-indirect-store
check 42 'int main() { int x = 42; int *p = &x; return *(p += 0); }' pointer-init-compound
check 24 'int main() { int x = 42; int *p; p = &x; *p = 24; return x; }'
check 42 'int main() { int x = 42; int *p; int *q; p = &x; q = p; return *q; }'
check 42 'int main() { int a = 24; int b = 42; int *p; p = &a; p = &b; return *p; }'
check 7 'int main() { int x = 42; int *p; int *q; p = &x; q = &x; *q = 7; return *p; }'
check 2 'int main() { int x = 1; int *p; p = &x; *p = *p + 1; return x; }'
check 42 'int main() { int x = 42; int *p; int **pp; p = &x; pp = &p; return **pp; }'
check 42 'int main() { int x = 42; int *p; p = &x; return *(&x); }'
check 42 'int main() { int x = 42; int *p; p = &x; *p = *p; return x; }'
# A scalar object permits pointer arithmetic up to one past its end.
check 24 'int main() { int a = 24; int *p = &a; return *((p + 1) - 1); }' pointer-add
check 24 'int main() { int a = 24; int *p = &a; return *((1 + p) - 1); }' integer-add-pointer
check 42 'int main() { int a = 42; int *p = &a + 1; return *(p - 1); }' pointer-subtract
check 1 'int main() { int a; int *p = &a + 1; int *q = &a; return p - q; }' pointer-difference-positive
check 255 'int main() { int a; int *p = &a + 1; int *q = &a; return q - p; }' pointer-difference-negative
check 3 'int main() { int a; int *p = &a + 1; int *q = &a; return (p - q) * 3; }' pointer-difference-scale
check 6 'int main() { int a; int *p = &a + 1; int *q = &a; return (p - q) * 6; }' pointer-difference-expression
check 7 'int main() { int a = 24; int *p = &a + 1; *(p - 1) = 7; return a; }' pointer-arithmetic-store
check 24 'int main() { int a = 24; int *p = &a; p++; p--; return *p; }' pointer-postincrement
check 42 'int main() { int a = 42; int *p = &a + 1; p--; return *p; }' pointer-postdecrement
check 24 'int main() { int a = 24; int *p = &a; ++p; --p; return *p; }' pointer-preincrement
check 6 'int main() { int a = 3; int *p = &a; int s = *p; p++; p--; return s + *p; }' pointer-increment-roundtrip
check 42 'int main() { return 1, 42; }'
check 42 'int main() { int a = 0; a = 1, a = a + 41; return a; }'
check 6 'int main() { int i; int j; int n = 0; for (i = 0, j = 0; i < 3; i = i + 1, j = j + 1) n = n + i + j; return n; }'
check 1 'int main() { int a = 0; a = 1, 2; return a; }'
check 42 'int main() { int a = 1; int b = 42; return (a, b); }'
check 42 'int main() { int a = 0; return 1 ? a = 1, a + 41 : 0; }'
check 42 'int main() { int a = 0; if (a = 1, a == 1) return 42; return 0; }'
check 42 'int main() { int a = 0; return a = 41, a + 1; }'
check 42 'int main() { switch (0) { default: return -1; case 0: return 42; } return -1; }'
check 42 $'int main() { // comment\r\n return 42; }' comment-crlf
check 42 $'int main() { // comment\r return 42; }' comment-cr
check 42 $'int\fmain() { return 42; }' whitespace-ff
check 42 $'int\vmain() { return 42; }' whitespace-vt
check 42 'int f(int); int f(int x) {return x;} int main() { return f(42); }' prototype-unamed-name
check 42 'int f(int y); int f(int x) {return x;} int main() { return f(42); }' prototype-different-param-name
check 42 'int f(); int f(int x) {return x;} int main() {return f(42);}' prototype-unspecified-params

# Expression builders must preserve pointer types through nested expressions.
check 42 'int main() { int x = 42; int y = 7; return *(1 ? &x : &y); }' conditional-pointer-type
check 42 'int main() { int x = 42; return *(0, &x); }' comma-pointer-type
check 42 'int main() { int x = 42; int *p; return *(p = &x); }' assignment-pointer-type
check 42 'int main() { int x = 42; int *p; p = &x; return *(p += 0); }' compound-pointer-type
check 42 'int main() { int x = 42; int *p; p = &x; return *p++; }' postfix-pointer-type
check 0 'int main() { int x; return (&x + 0) - &x; }' binary-pointer-type

# Typed functions preserve pointer widths and check complete signatures.
check 42 'int *identity(int *p) { return p; } int main(void) { int x = 42; return *identity(&x); }' typed-pointer-identity
check 42 'int *identity(int *); int main(void) { int x = 42; return *identity(&x); } int *identity(int *p) { return p; }' typed-pointer-prototype
check 42 'int **identity(int **p) { return p; } int main(void) { int x = 42; int *p = &x; return **identity(&p); }' typed-nested-pointer
check 42 'int get(int *p, int n) { return *p + n; } int main(void) { int x = 43; return get(&x, -1); }' typed-mixed-parameters
check 42 'int get(int *a, int b, int *c, int d, int *e, int f) { return *a + b + *c + d + *e + f; } int main(void) { int x = 10; return get(&x, 1, &x, 2, &x, 9); }' typed-six-registers
check 42 'void set(int *p, int n) { *p = n; return; } int main(void) { int x = 0; set(&x, 42); return x; }' typed-void-return
check 42 'void set(int *p) { *p = 42; } int main(void) { int x = 0; set(&x); return x; }' typed-void-fallthrough
check 1 'int *nothing(void) { return 0; } int main(void) { return nothing() == 0; }' typed-null-return
check 1 'int isnull(int *p) { return p == 0; } int main(void) { return isnull(1 - 1); }' typed-null-argument
check 42 'void *identity(void *p) { return p; } int main(void) { int x = 42; int *p = identity(&x); return *p; }' typed-void-pointer-conversion
check 42 'int *identity(int *p) { return p; } int main(void) { int x = 42; void *p = &x; return *identity(p); }' typed-void-pointer-argument
check 42 'int *identity(int *p) { return p; } int main(void) { int x = 42; return *identity(identity(&x)); }' typed-nested-calls
check 42 'int *walk(int *p, int n) { if (n) return walk(p, n - 1); return p; } int main(void) { int x = 42; return *walk(&x, 3); }' typed-pointer-recursion
check 42 'int *identity(); int main(void) { int x = 42; return *identity(&x); } int *identity(int *p) { return p; }' typed-unspecified-prototype
check 42 'int *identity(int *); int *identity(int *value); int *identity(int *p) { return p; } int main(void) { int x = 42; return *identity(&x); }' typed-compatible-redeclarations
check 42 'int *identity(int *p) { return p; } int *identity(); int main(void) { int x = 42; return *identity(&x); }' typed-preserve-prototype
check 42 'int *choose(int *p) { return 0 ? 0 : p; } int main(void) { int x = 42; return *choose(&x); }' typed-null-conditional
check 42 'void noop(void) { return; } int main(void) { noop(), noop(); 1 ? noop() : noop(); return 42; }' typed-discard-void
check_error typed-conflicting-return 'int *f(int *p); int f(int *p);' '1:21: error: conflicting declaration of function '\''f'\'''
check_error typed-conflicting-parameter 'int f(int *p); int f(int p);' '1:20: error: conflicting declaration of function '\''f'\'''
check_error typed-conflicting-pointer-depth 'int f(int **); int f(int *);' '1:20: error: conflicting declaration of function '\''f'\'''
check_error typed-conflicting-void-pointer 'int f(void *); int f(int *);' '1:20: error: conflicting declaration of function '\''f'\'''
check_error typed-nonzero-pointer-argument 'int f(int *p) { return 0; } int main(void) { return f(1); }' '1:55: error: incompatible argument type'
check_error typed-variable-zero-argument 'int f(int *p) { return 0; } int main(void) { int x = 0; return f(x); }' '1:66: error: incompatible argument type'
check_error typed-pointer-to-int-argument 'int f(int n) { return n; } int main(void) { int x = 0; return f(&x); }' '1:65: error: incompatible argument type'
check_error typed-wrong-pointer-depth-argument 'int f(int **p) { return 0; } int main(void) { int *p = 0; return f(p); }' '1:68: error: incompatible argument type'
check_error typed-nonzero-pointer-return 'int *f(void) { return 1; }' '1:16: error: incompatible return type'
check_error typed-pointer-to-int-return 'int f(int *p) { return p; }' '1:17: error: incompatible return type'
check_error typed-void-return-value 'void f(void) { return 1; }' '1:16: error: return with a value in void function'
check_error typed-void-value-return 'void f(void) {} int main(void) { return f(); }' '1:41: error: void expression used as a value'
check_error typed-void-initializer 'void f(void) {} int main(void) { int x = f(); return 0; }' '1:42: error: void expression used as a value'
check_error typed-void-condition 'void f(void) {} int main(void) { if (f()) return 1; return 0; }' '1:38: error: void expression used as a value'
check_error typed-void-arithmetic 'void f(void) {} int main(void) { return f() + 1; }' '1:41: error: void expression used as a value'
check_error typed-void-variable 'int main(void) { void x; return 0; }' '1:23: error: variable has void type'
check_error typed-void-parameter 'int f(void x);' '1:12: error: parameter has void type'
check_error typed-pointer-arity 'int f(int *); int main(void) { return f(); }' '1:39: error: wrong number of arguments to function '\''f'\'''
check_error typed-deferred-argument 'int f(); int main(void) { return f(1); } int f(int *p) { return 0; }' '1:36: error: incompatible argument type'
check_error typed-void-double-pointer 'int f(void **p) { return 0; } int main(void) { int *p = 0; return f(&p); }' '1:69: error: incompatible argument type'
check_error typed-missing-pointer-return 'int *f(void) { return; }' '1:22: error: expect return expr'

check 1 'int f(int *p) { return p == 0; } int main(void) { return f(1 ? 0 : 1 / 0); }' typed-conditional-null
check 42 'int *f(int *p) { return p; } int main(void) { int x = 42; return *(f(&x) + 0); }' typed-call-pointer-arithmetic
check_error typed-void-pointer-increment 'int main(void) { void *p = 0; p++; return 0; }' '1:32: error: pointer arithmetic on void pointer'
check_error typed-void-pointer-compound 'int main(void) { void *p = 0; p += 1; return 0; }' '1:33: error: pointer arithmetic on void pointer'
check_error typed-void-pointer-add 'int main(void) { void *p = 0; p + 1; return 0; }' '1:33: error: pointer arithmetic on void pointer'
check_error typed-void-pointer-deref 'int main(void) { void *p = 0; *p; return 0; }' '1:31: error: invalid deref target'
check_error typed-void-compound-value 'void f(void) {} int main(void) { int x = 0; x += f(); return x; }' '1:50: error: void expression used as a value'

# Arguments beyond the sixth use the stack; signatures grow as needed.
check 39 'int f(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8); int main(void) { return f(1, 2, 3, 4, 5, 6, 7, 8); } int f(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) { return a1 + 2 * a7 + 3 * a8; }' stack-arguments-8
check 42 'int f(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9); int main(void) { return f(1, 2, 3, 4, 5, 6, 7, 8, 9); } int f(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9) { return a1 + 2 * a7 + 3 * a9; }' stack-arguments-9
check 63 'int f(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16); int main(void) { return f(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16); } int f(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16) { return a1 + 2 * a7 + 3 * a16; }' stack-arguments-16
check 75 'int f(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16, int a17, int a18, int a19, int a20); int main(void) { return f(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20); } int f(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16, int a17, int a18, int a19, int a20) { return a1 + 2 * a7 + 3 * a20; }' stack-arguments-20
check 135 'int f(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16, int a17, int a18, int a19, int a20, int a21, int a22, int a23, int a24, int a25, int a26, int a27, int a28, int a29, int a30, int a31, int a32, int a33, int a34, int a35, int a36, int a37, int a38, int a39, int a40); int main(void) { return f(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40); } int f(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16, int a17, int a18, int a19, int a20, int a21, int a22, int a23, int a24, int a25, int a26, int a27, int a28, int a29, int a30, int a31, int a32, int a33, int a34, int a35, int a36, int a37, int a38, int a39, int a40) { return a1 + 2 * a7 + 3 * a40; }' stack-arguments-40
check 42 'int f(int a, int b, int c, int d, int e, int f, int g) { return -g; } int main(void) { return f(1, 2, 3, 4, 5, 6, -42); }' stack-argument-negative
check 42 'int *f(int a, int b, int c, int d, int e, int f, int *g) { return g; } int main(void) { int x = 42; return *f(1, 2, 3, 4, 5, 6, &x); }' stack-argument-pointer
check 42 'int **f(int a, int b, int c, int d, int e, int f, int g, int **h) { return h; } int main(void) { int x = 42; int *p = &x; return **f(1, 2, 3, 4, 5, 6, 7, &p); }' stack-argument-double-pointer
check 1 'int f(int a, int b, int c, int d, int e, int f, int *g) { return g == 0; } int main(void) { return f(1, 2, 3, 4, 5, 6, 0); }' stack-argument-null
check 42 'void set(int *p) { *p = 42; } int f(int a, int b, int c, int d, int e, int f, int g) { set(&g); return g; } int main(void) { return f(1, 2, 3, 4, 5, 6, 7); }' stack-argument-address
check 42 'int f(int a, int b, int c, int d, int e, int z, int g) { if (a) return f(a - 1, b, c, d, e, z, g); return g; } int main(void) { return f(4, 2, 3, 4, 5, 6, 42); }' stack-argument-recursion
check 42 'int f(int a, int b, int c, int d, int e, int f, int g) { return g; } int main(void) { int i; int n = 0; for (i = 0; i < 100; i++) n = f(1, 2, 3, 4, 5, 6, 42); return n; }' stack-argument-cleanup
check 42 'int f(int a, int b, int c, int d, int e, int f, int g) { return a + g; } int main(void) { return f(1, 2, 3, 4, 5, 6, f(7, 8, 9, 10, 11, 12, 34)); }' stack-argument-nested-call
check 42 'int f(int a, int b, int c, int d, int e, int f, int g) { return a + g; } int main(void) { int x = 20; return f(20, 2, 3, 4, 5, 6, (x += 2)); }' stack-argument-side-effect
check_error stack-argument-too-few 'int f(int, int, int, int, int, int, int); int main(void) { return f(1,2,3,4,5,6); }' '1:67: error: wrong number of arguments to function '\''f'\'''
check_error stack-argument-too-many 'int f(int, int, int, int, int, int, int); int main(void) { return f(1,2,3,4,5,6,7,8); }' '1:67: error: wrong number of arguments to function '\''f'\'''
check_error stack-argument-type 'int f(int, int, int, int, int, int, int *); int main(void) { return f(1,2,3,4,5,6,42); }' '1:83: error: incompatible argument type'
check_error stack-parameter-conflict 'int f(int, int, int, int, int, int, int); int f(int, int, int, int, int, int, int *);' '1:47: error: conflicting declaration of function '\''f'\'''

# Plain char is a signed byte, promoted to int for arithmetic.
check 42 'int main(void) { char c = 42; return c; }' 'char-local'
check 1 'char identity(char c) { return c; } int main(void) { char x = 42; char *p = &x; *p = identity(255); return x == -1; }' 'char-identity'
check 42 'int main(void) { char a = 42; char b = 7; return a; }' 'char-adjacent-initializers'
check 42 'int main(void) { char a = 42; char b = 7; b = 8; return a; }' 'char-adjacent-assignment'
check 42 'int main(void) { char a = 42; char b = 7; char *p = &b; *p = 8; return a; }' 'char-adjacent-indirect-store'
check 1 'int main(void) { int x = 42; char a = 7; char b = -1; return x == 42 && a == 7 && b == -1; }' 'char-mixed-local-layout'
check 254 'int main(void) { char a = 127; char b = 127; return a + b; }' 'char-addition-promotion'
check 128 'int main(void) { char a = -128; return -a; }' 'char-negation-promotion'
check 1 'int main(void) { char a = -1; return ~a == 0 && a < 0; }' 'char-bitwise-and-comparison'
check 1 'int main(void) { char a = -128; char b = 1; return (a >> b) == -64; }' 'char-shift-promotion'
check 1 'int main(void) { char a = 127; char b = 1; return (1 ? a : b) + 1 == 128; }' 'char-conditional-promotion'
check 1 'int main(void) { char a = -1; return (0 ? 42 : a) == -1; }' 'char-mixed-conditional'
check 1 'int main(void) { char c = 0; int x = (c = 255); return c == -1 && x == -1; }' 'char-assignment-result'
check 1 'int main(void) { char c = 127; return (c += 1) == -128 && c == -128; }' 'char-compound-narrowing'
check 1 'int main(void) { char c = 100; return (c *= 3) == 44; }' 'char-compound-promotion'
check 1 'int main(void) { char c = 127; return ++c == -128 && c == -128; }' 'char-preincrement-boundary'
check 1 'int main(void) { char c = -128; return --c == 127 && c == 127; }' 'char-predecrement-boundary'
check 1 'int main(void) { char c = 127; int old = c++; return old == 127 && c == -128; }' 'char-postincrement-boundary'
check 1 'int main(void) { char c = -128; int old = c--; return old == -128 && c == 127; }' 'char-postdecrement-boundary'
check 42 'int main(void) { char a = 42; char b = 7; char *p = &b; ++*p; (*p)--; return a; }' 'char-adjacent-increments'
check 1 'int main(void) { int x = 42; void *v = &x; char *p = v; *(p + 1) = 1; return x == 298; }' 'char-pointer-byte-stride'
check 1 'int main(void) { int x = 42; void *v = &x; char *p = v; char *q = p; p++; return p - q == 1; }' 'char-pointer-increment'
check 42 'int main(void) { char x = 42; char *p = &x; char **pp = &p; return **pp; }' 'char-nested-pointer-load'
check 1 'int take(char c) { return c; } int main(void) { return take(255) == -1; }' 'char-parameter-narrowing'
check 1 'char give(int n) { return n; } int main(void) { return give(255) == -1; }' 'char-return-narrowing'
check 1 'char give(void) { return -128; } int main(void) { return give() < 0; }' 'char-call-sign-extension'
check 1 'char f(char a, char b, char c, char d, char e, char f, char g, char h) { return g + h; } int main(void) { return f(1,2,3,4,5,6,255,128) == 127; }' 'char-stack-parameters'
check 42 'int f(int a, char b, int *p, char d, int e, char f, char *g, char h) { return a + b + *p + d + e + f + *g + h; } int main(void) { int x = 10; char y = 10; return f(1,2,&x,3,4,5,&y,7); }' 'char-mixed-parameters'
check 42 'int f(); int main(void) { char c = 42; return f(c); } int f(int n) { return n; }' 'char-default-argument-promotion'
check 1 'int main(void) { int total = 0; for (char c = 0; c < 10; c++) total += c; return total == 45; }' 'char-for-declaration'
check 42 'int main(void) { char c = 255; switch (c) { case -1: return 42; default: return 0; } }' 'char-switch'
check 1 'int main(void) { char c = -1; return !c == 0 && (c || 0) && (c && 1); }' 'char-logical-operators'
check 1 'int main(void) { char c = 0; return (c = (c = 255)) == -1; }' 'char-chained-assignment'
check 1 'int main(void) { char c = 127; return c == 127; }' 'char-initializer-boundary-127'
check 1 'int main(void) { char c = 128; return c == -128; }' 'char-initializer-boundary-128'
check 1 'int main(void) { char c = 255; return c == -1; }' 'char-initializer-boundary-255'
check 1 'int main(void) { char c = 256; return c == 0; }' 'char-initializer-boundary-256'
check 1 'int main(void) { char c = -129; return c == 127; }' 'char-initializer-boundary--129'
check 1 'int main(void) { char c = -128; return c == -128; }' 'char-initializer-boundary--128'
check 1 'int main(void) { char c = -256; return c == 0; }' 'char-initializer-boundary--256'
check 1 'int main(void) { char c = 127; c += 1; return c == -128; }' 'char-compound-+='
check 1 'int main(void) { char c = -128; c -= 1; return c == 127; }' 'char-compound--='
check 1 'int main(void) { char c = 100; c *= 3; return c == 44; }' 'char-compound-*='
check 1 'int main(void) { char c = -127; c /= 2; return c == -63; }' 'char-compound-/='
check 1 'int main(void) { char c = -127; c %= 2; return c == -1; }' 'char-compound-%='
check 1 'int main(void) { char c = 64; c <<= 1; return c == -128; }' 'char-compound-<<='
check 1 'int main(void) { char c = -128; c >>= 1; return c == -64; }' 'char-compound->>='
check 1 'int main(void) { char c = -1; c &= 127; return c == 127; }' 'char-compound-&='
check 1 'int main(void) { char c = 127; c ^= 255; return c == -128; }' 'char-compound-^='
check 1 'int main(void) { char c = 0; c |= 255; return c == -1; }' 'char-compound-|='
check_error char-conflicting-parameter 'int f(char); int f(int);' '1:18: error: conflicting declaration of function '\''f'\'''
check_error char-conflicting-return 'char f(void); int f(void);' '1:19: error: conflicting declaration of function '\''f'\'''
check_error char-prototype-default-promotion 'int f(); int f(char);' '1:14: error: conflicting declaration of function '\''f'\'''
check_error char-prototype-default-promotion-reverse 'int f(char); int f();' '1:18: error: conflicting declaration of function '\''f'\'''
check_error char-incompatible-pointer 'int main(void) { char *p; int *q = p; return 0; }' '1:32: error: incompatible initializer type'
check_error char-not-null-pointer-constant 'int main(void) { char c = 0; int *p = c; return 0; }' '1:35: error: incompatible initializer type'
check_error char-pointer-assignment 'int main(void) { char c; int *p = 0; c = p; return 0; }' '1:40: error: incompatible assignment type'

# Ordinary character constants are int tokens; byte escapes follow signed char.
check 65 "int main(void) { return 'A'; }" character-literal-letter
check 48 "int main(void) { return '0'; }" character-literal-digit
check 32 "int main(void) { return ' '; }" character-literal-space
check 34 "int main(void) { return '\"'; }" character-literal-double-quote
check 39 "int main(void) { return '\\''; }" character-literal-escaped-quote
check 34 "int main(void) { return '\\\"'; }" character-literal-escaped-double-quote
check 92 "int main(void) { return '\\\\'; }" character-literal-backslash
check 63 "int main(void) { return '\\?'; }" character-literal-question
check 7 "int main(void) { return '\\a'; }" character-literal-alert
check 8 "int main(void) { return '\\b'; }" character-literal-backspace
check 12 "int main(void) { return '\\f'; }" character-literal-form-feed
check 10 "int main(void) { return '\\n'; }" character-literal-newline
check 13 "int main(void) { return '\\r'; }" character-literal-carriage-return
check 9 "int main(void) { return '\\t'; }" character-literal-tab
check 11 "int main(void) { return '\\v'; }" character-literal-vertical-tab
check 0 "int main(void) { return '\\0'; }" character-literal-null
check 7 "int main(void) { return '\\7'; }" character-literal-octal-one-digit
check 10 "int main(void) { return '\\12'; }" character-literal-octal-two-digits
check 65 "int main(void) { return '\\101'; }" character-literal-octal-three-digits
check 65 "int main(void) { return '\\x41'; }" character-literal-hex
check 65 "int main(void) { return '\\x00000041'; }" character-literal-hex-leading-zeroes
check 1 "int main(void) { return '\\xff' == -1 && '\\377' == -1 && '\\x80' == -128 && '\\200' == -128; }" character-literal-signed-byte
check 1 "int main(void) { char c = 'A'; return c == 65 && 'A' + 200 == 265; }" character-literal-char-initialization
check 42 "int main(void) { switch ('\\n') { case 10: return 42; default: return 0; } }" character-literal-switch
check 42 "int main(void) { switch (10) { case '\\n': return 42; default: return 0; } }" character-literal-case
check 89 "int main(void) { return '/' + '*'; }" character-literal-comment-characters
check 1 "char identity(char c) { return c; } int main(void) { return identity('\\xff') == -1; }" character-literal-function-argument
check_error character-literal-empty "int main(void) { return ''; }" "1:25: error: empty character literal"
check_error character-literal-multiple "int main(void) { return 'ab'; }" "1:25: error: multi-character literals not supported"
check_error character-literal-invalid-escape "int main(void) { return '\\q'; }" "1:25: error: invalid escape sequence in character literal"
check_error character-literal-empty-hex "int main(void) { return '\\x'; }" "1:25: error: expect hexadecimal digit in character escape"
check_error character-literal-invalid-hex "int main(void) { return '\\xg'; }" "1:25: error: expect hexadecimal digit in character escape"
check_error character-literal-octal-overflow "int main(void) { return '\\400'; }" "1:25: error: character escape out of range"
check_error character-literal-hex-overflow "int main(void) { return '\\x100'; }" "1:25: error: character escape out of range"
check_error character-literal-long-hex-overflow "int main(void) { return '\\xffffffffffffffffffff'; }" "1:25: error: character escape out of range"
check_error character-literal-octal-limit "int main(void) { return '\\1234'; }" "1:25: error: multi-character literals not supported"
check_error character-literal-eof "int main(void) { return '" "1:25: error: unterminated character literal"
check_error character-literal-missing-quote "int main(void) { return 'a; }" "1:25: error: unterminated character literal"
check_error character-literal-escape-eof "int main(void) { return '\\" "1:25: error: unterminated character literal"
check_error character-literal-raw-newline $'int main(void) { return \'\n\'; }' "1:25: error: unterminated character literal"
check_error character-literal-raw-crlf $'int main(void) { return \'\r\n\'; }' "1:25: error: unterminated character literal"
check_error character-literal-escaped-newline $'int main(void) { return \'\\\n\'; }' "1:25: error: unterminated character literal"

# Full unsigned short int and unsigned long int spellings.
check 1 'int main(void) { unsigned short int x = -1; return x == 65535 && x + 1 == 65536; }' unsigned-short-int-promotion
check 1 'int main(void) { unsigned short int x = 65537; x += 65535; return x == 0; }' unsigned-short-int-truncation
check 1 'int main(void) { unsigned short int guard = 42; unsigned short int x = 0; unsigned short int *p = &x; return (*p = -1) == 65535 && x == 65535 && guard == 42; }' unsigned-short-int-pointer-store
check 1 'unsigned short int id(unsigned short int); unsigned short int id(unsigned short int x) { return x; } int main(void) { return id(-1) == 65535 && id(65536) == 0; }' unsigned-short-int-function-signature
check 1 'int main(void) { unsigned long int x = 1; x <<= 40; return x >> 40 == 1; }' unsigned-long-int-width
check 1 'int main(void) { unsigned long int x = -1; return x + 1 == 0 && x / 2 >> 62 == 1; }' unsigned-long-int-arithmetic
check 1 'unsigned long int *id(unsigned long int *p) { return p; } int main(void) { unsigned long int x = 1; x <<= 40; unsigned long int y = x; *id(&x) = x + 7; return x == y + 7; }' unsigned-long-int-pointer-store
check 1 'unsigned long int id(unsigned long int); unsigned long int id(unsigned long int x) { return x; } int main(void) { unsigned long int x = 1; x <<= 40; return id(x) == x && id(-1) >> 63 == 1; }' unsigned-long-int-function-signature
check 1 'unsigned long int f(int a, int b, int c, int d, int e, int f, unsigned short int g, unsigned long int h) { return g + h; } int main(void) { unsigned long int x = 1; x <<= 40; return f(1,2,3,4,5,6,-1,x) == x + 65535; }' unsigned-short-long-int-stack-arguments

# Short storage, conversions, and promotions use the target's 16-bit width.
check 1 'int main(void) { short s = 32767; return s == 32767; }' short-load
check 1 'int main(void) { short s = -32768; return s == -32768 && s < 0; }' short-sign-extension
check 1 'int main(void) { unsigned short s = 65535; return s == 65535 && s > 0; }' unsigned-short-zero-extension
check 1 'int main(void) { short s = 65535; return s == -1; }' short-initializer-truncate
check 1 'int main(void) { unsigned short s = -1; return s == 65535; }' unsigned-short-initializer-truncate
check 1 'int main(void) { short s = 65536; unsigned short u = 65537; return s == 0 && u == 1; }' short-initializer-wrap
check 1 'int main(void) { short s = 0; unsigned short u = 0; return (s = 65535) == -1 && (u = -1) == 65535; }' short-assignment-result
check 1 'int main(void) { int guard = 42; short a = -1; short b = 7; b = 9; return guard == 42 && a == -1 && b == 9; }' short-adjacent-stores
check 1 'int main(void) { char a = 42; short b = -1; char c = 7; short d = 9; d = -2; return a == 42 && b == -1 && c == 7 && d == -2; }' short-mixed-local-layout
check 1 'int main(void) { unsigned short s = 65535; return s + 1 == 65536 && s > -1; }' unsigned-short-integer-promotion
check 1 'int main(void) { unsigned short s = 65535; return -s == -65535 && ~s == -65536; }' unsigned-short-unary-promotion
check 1 'int main(void) { short s = -32768; return -s == 32768 && s / -1 == 32768; }' short-unary-division-promotion
check 1 'int main(void) { short s = -1; unsigned short u = 65535; return u + s == 65534 && u > s; }' short-mixed-signs
check 1 'int main(void) { unsigned short s = 65535; unsigned u = -1; return s < u && s + u == 65534; }' short-unsigned-common-type
check 1 'int main(void) { unsigned short s = 65535; return (1 ? s : -1) == 65535 && (0 ? s : -1) < 0; }' short-conditional-promotion
check 1 'int main(void) { short s = -1; unsigned short u = 65535; long a = s; long b = u; return a == -1 && b == 65535; }' short-widen-long
check 1 'int main(void) { long x = 1; x <<= 40; short s = x + 65535; unsigned short u = x + 65537; return s == -1 && u == 1; }' long-narrow-short
check 1 'int main(void) { unsigned x = -1; short s = x; unsigned short u = x; return s == -1 && u == 65535; }' unsigned-int-narrow-short
check 1 'int main(void) { short s = -1; char c = s; unsigned char u = s; unsigned short v = c; return c == -1 && u == 255 && v == 65535; }' short-char-conversions
check 1 'int main(void) { short s = 32767; s += 1; s -= 1; return s == 32767; }' short-compound-truncate
check 1 'int main(void) { unsigned short s = 65535; s += 1; s -= 1; s *= 2; s /= 2; s %= 7; return s == 0; }' short-compound-arithmetic
check 1 'int main(void) { unsigned short s = 65535; s <<= 1; s >>= 1; s |= 7; s ^= 8; s &= 255; return s == 247; }' short-compound-bitwise
check 1 'int main(void) { short s = 32767; return s++ == 32767 && s == -32768 && --s == 32767 && ++s == -32768 && s-- == -32768 && s == 32767; }' short-increment-boundaries
check 1 'int main(void) { unsigned short s = 65535; return s++ == 65535 && s == 0 && --s == 65535 && ++s == 0 && s-- == 0 && s == 65535; }' unsigned-short-increment-boundaries
check 1 'int main(void) { short guard = 42; short s = 0; short *p = &s; return (*p = 65535) == -1 && s == -1 && guard == 42; }' short-indirect-store
check 1 'int main(void) { unsigned short s = 65535; unsigned short *p = &s; return (*p)++ == 65535 && s == 0 && --*p == 65535; }' short-indirect-increment
check 42 'int main(void) { short s = 42; short *p = &s + 1; short n = -1; return *(p + n); }' short-pointer-offset
check 1 'int main(void) { unsigned short s = 32768; if (!s) return 0; return s && (0 || s) && (s ? 1 : 0); }' short-condition
check 42 'int main(void) { unsigned short s = -1; switch (s) { case -1: return 0; case 65535: return 42; default: return 0; } }' short-switch-promotion
check 2 'int main(void) { int n = 0; for (unsigned short s = 65534; s; s++) n++; return n; }' short-for-counter
check 1 'short id(short s) { return s; } int main(void) { return id(65535) == -1 && id(32768) == -32768; }' short-call-return
check 1 'unsigned short id(unsigned short s) { return s; } int main(void) { return id(-1) == 65535 && id(65536) == 0 && id(-1) + 1 == 65536; }' unsigned-short-call-return
check 1 'short f(void) { return 65535; } unsigned short g(void) { return -1; } int main(void) { return f() == -1 && g() == 65535; }' short-return-conversion
check 1 'int f(short a, unsigned short b, short c, unsigned short d, short e, unsigned short f) { return a+b+c+d+e+f; } int main(void) { return f(-1,65535,-2,32768,-3,12345) == 110642; }' short-six-register-arguments
check 1 'int f(int a, int b, int c, int d, int e, int f, short g, unsigned short h) { return a+g+h; } int main(void) { return f(1,2,3,4,5,6,32768,-1) == 32768; }' short-stack-arguments
check 1 'long f(short a, unsigned short b, char c, int d, long e, unsigned long f, int g, short h, unsigned short i) { return a+b+c+d+e+f+g+h+i; } int main(void) { long x = 1; x <<= 40; return f(-1,-1,-2,3,x,0,0,32768,32768) == x + 65535; }' short-mixed-stack-arguments
check 1 'void set(short *p) { *p = -1; } short f(short s) { set(&s); return s; } int main(void) { return f(42) == -1; }' short-parameter-address
check 1 'int f(); int main(void) { unsigned short s = 65535; return f(s); } int f(int s) { return s == 65535; }' short-default-argument-promotion
check 1 'unsigned short id(unsigned short s) { return s; } int main(void) { return id(id(-1)) == 65535; }' short-nested-call
check 120 'short fact(short n) { if (n == 0) return 1; return n * fact(n - 1); } int main(void) { return fact(5); }' short-recursion

# Short declaration specifiers.
for spec in 'short' 'short int' 'int short' 'unsigned short' 'short unsigned' \
    'unsigned short int' 'unsigned int short' 'short unsigned int' \
    'short int unsigned' 'int unsigned short' 'int short unsigned'; do
    check 42 "$spec f($spec); int main(void) { $spec x; return 42; }" "short-declspecs-$spec"
done
check 42 'short f(short int); short int f(int short); int main(void) { return 42; }' short-compatible-prototypes
check 42 'unsigned short f(short unsigned int); int unsigned short f(unsigned int short); int main(void) { return 42; }' unsigned-short-compatible-prototypes
check 42 'int main(void) { for (short i; 0;) ; return 42; }' short-for-declaration
check 42 'int main(void) { for (unsigned short i; 0;) ; return 42; }' unsigned-short-for-declaration
check 1 'int main(void) { short x; short *p = &x; return (p + 1) - p; }' short-pointer-declaration

check_error short-missing-name 'int main() { int short; return 0; }' '1:23: error: expect variable name'
check_error short-char-specifiers 'int main(void) { short char x; return 0; }' '1:24: error: invalid combination of type specifiers'
check_error char-short-specifiers 'int main(void) { char short x; return 0; }' '1:23: error: invalid combination of type specifiers'
check_error short-long-specifiers 'int main(void) { short long x; return 0; }' '1:24: error: invalid combination of type specifiers'
check_error long-short-specifiers 'int main(void) { long short x; return 0; }' '1:23: error: invalid combination of type specifiers'
check_error duplicate-short-specifier 'int main(void) { short short x; return 0; }' '1:24: error: duplicate short type specifier'
check_error short-duplicate-int 'int main(void) { short int int x; return 0; }' '1:28: error: duplicate int type specifier'
check_error short-duplicate-unsigned 'int main(void) { unsigned short unsigned x; return 0; }' '1:33: error: duplicate unsigned type specifier'
check_error short-conflicting-return 'short f(void); int f(void);' '1:20: error: conflicting declaration of function '"'"'f'"'"''
check_error short-conflicting-parameter 'int f(short); int f(int);' '1:19: error: conflicting declaration of function '"'"'f'"'"''
check_error short-conflicting-signedness 'int f(short); int f(unsigned short);' '1:19: error: conflicting declaration of function '"'"'f'"'"''
check_error short-no-prototype-promotion 'int f(); int f(short);' '1:14: error: conflicting declaration of function '"'"'f'"'"''
check_error short-no-prototype-promotion-reverse 'int f(short); int f();' '1:19: error: conflicting declaration of function '"'"'f'"'"''
check_error unsigned-short-no-prototype-promotion 'int f(); int f(unsigned short);' '1:14: error: conflicting declaration of function '"'"'f'"'"''
check_error short-incompatible-pointer 'int main(void) { int x; short *p = &x; return 0; }' '1:32: error: incompatible initializer type'
check_error short-declaration-as-statement 'int main(void) { if (1) short x; return 0; }' '1:25: error: expect statement'

# Long values exercise the upper 32 bits without requiring long literals.
check 1 'int main(void) { long x = 1; x <<= 40; long y = x; return y >> 40 == 1; }' long-load-store
check 1 'int main(void) { long x = -1; long y = 0 - 1; return x < 0 && y < 0; }' long-sign-extension
check 1 'int main(void) { unsigned x = -1; long y = x; return y > 2147483647 && y >> 32 == 0; }' long-zero-extension
check 1 'int main(void) { long x = -2147483647 - 1; return x < -2147483647; }' long-int-min-extension
check 1 'int main(void) { unsigned long x = -1; return x >> 63 == 1; }' unsigned-long-sign-extension
check 1 'int main(void) { long x = 1; x <<= 40; long y; return (y = x) == x && y >> 40 == 1; }' long-assignment-result
check 1 'int main(void) { long x = 1; x <<= 40; return x + x == x * 2 && x + 7 - x == 7; }' long-add-subtract-multiply
check 1 'int main(void) { long x = 1; x <<= 40; return (x * 6) / 3 == x * 2 && (x + 5) % x == 5; }' long-divmod
check 1 'int main(void) { long x = 1; x <<= 40; return (-x - 5) / x == -1 && (-x - 5) % x == -5; }' long-negative-divmod
check 1 'int main(void) { unsigned long x = -1; return x / 2 >> 62 == 1 && x % 2 == 1; }' unsigned-long-divmod
check 1 'int main(void) { unsigned long x = -1; return x + 1 == 0 && x * x == 1 && -x == 1; }' unsigned-long-wraparound
check 1 'int main(void) { long x = 1; x <<= 40; return x != 0 && x > 2147483647 && x >= x && 0 < x && 0 <= x; }' long-comparisons
check 1 'int main(void) { unsigned long x = -1; long y = -1; return x > 0 && x >= y && y > 0 - x && x == y; }' unsigned-long-comparisons
check 1 'int main(void) { long x = -1; unsigned y = 1; return x < y && x + y == 0 && x <= y; }' long-unsigned-int-common-type
check 1 'int main(void) { long x = -1; unsigned long y = 1; return x > y && x + y == 0; }' long-unsigned-long-common-type
check 1 'int main(void) { long x = 0; unsigned y = -1; return (x | y) >> 32 == 0 && (x ^ y) >> 32 == 0; }' long-unsigned-bitwise-conversion
check 1 'int main(void) { long x = 1; x <<= 40; return (x | 3) == x + 3 && (x ^ 3) == x + 3 && ((x | 3) & x) == x; }' long-bitwise
check 1 'int main(void) { long x = 1; x <<= 40; return ~x == -x - 1 && -x < 0; }' long-unary
check 1 'int main(void) { long x = -1; unsigned y = 40; return x >> y == -1; }' long-signed-shift
check 1 'int main(void) { unsigned long x = -1; return x >> 63 == 1 && x << 63 >> 63 == 1; }' unsigned-long-shifts
check 1 'int main(void) { long n = 1; return (-4 >> n) == -2; }' int-shift-long-count
check 1 'int main(void) { long x = 1; x <<= 40; return (1 ? x : 0) == x && (0 ? 0 : x) == x; }' long-conditional-type
check 1 'int main(void) { long x = -1; unsigned y = 0; return (1 ? x : y) < 0 && (0 ? y : x) < 0; }' long-conditional-signedness
check 1 'int main(void) { long x = 1; x <<= 40; return !x == 0 && !!x && (x && 1) && (0 || x); }' long-logical
check 42 'int main(void) { long x = 1; x <<= 32; if (x) return 42; return 0; }' long-if-condition
check 42 'int main(void) { long x = 1; x <<= 32; return x ? 42 : 0; }' long-conditional-condition
check 42 'int main(void) { long x = 1; x <<= 32; while (x) { x = 0; return 42; } return 0; }' long-while-condition
check 42 'int main(void) { long x = 1; x <<= 32; for (; x;) { x = 0; return 42; } return 0; }' long-for-condition
check 2 'int main(void) { long x = 1; x <<= 32; int n = 0; do { n++; if (n == 2) x = 0; } while (x); return n; }' long-do-condition
check 42 'int main(void) { long x = 1; x <<= 32; switch (x) { case 0: return 0; default: return 42; } }' long-switch-full-width
check 42 'int main(void) { long x = -1; switch (x) { case -1: return 42; default: return 0; } }' long-switch-negative
check 42 'int main(void) { unsigned long x = -1; switch (x) { case -1: return 42; default: return 0; } }' unsigned-long-switch-negative
check 1 'int main(void) { long x = 1; x <<= 40; int y = x + 42; unsigned z = x + 43; return y == 42 && z == 43; }' long-narrow-int
check 1 'int main(void) { long x = -1; char c = x; unsigned char u = x; return c == -1 && u == 255; }' long-narrow-char
check 1 'int main(void) { char c = -1; unsigned char u = 255; long x = c; long y = u; return x == -1 && y == 255; }' long-widen-char
check 1 'int main(void) { long x = 1; x <<= 40; long y = x; x += 7; x -= 1; x *= 3; x /= 3; return x == y + 6; }' long-compound-arithmetic
check 1 'int main(void) { unsigned long x = -1; x >>= 32; x <<= 8; x |= 15; x ^= 8; x &= -1; x %= 16; return x == 7; }' long-compound-bitwise
check 1 'int main(void) { long x = 1; x <<= 40; long y = x; return x++ == y && ++x == y + 2 && x-- == y + 2 && --x == y; }' long-increment
check 1 'int main(void) { unsigned long x = -1; return x++ != 0 && x == 0 && --x != 0; }' unsigned-long-increment
check 1 'int main(void) { long x = 1; x <<= 40; long *p = &x; long y = *p; *p = x + 7; return x == y + 7 && (*p)++ == y + 7 && --*p == y + 7; }' long-indirect-load-store-increment
check 42 'int main(void) { long x = 42; long *p = &x + 1; long n = -1; return *(p + n); }' long-pointer-offset
check 1 'int main(void) { long x; unsigned y = 0; return (&x - (&x + 1)) < y; }' pointer-difference-long-type
check 1 'long id(long x) { return x; } int main(void) { long x = 1; x <<= 40; return id(x) == x && id(-1) < 0; }' long-call-return
check 1 'unsigned long id(unsigned long x) { return x; } int main(void) { return id(-1) >> 63 == 1; }' unsigned-long-call-return
check 1 'long f(void) { return -1; } int main(void) { return f() < 0; }' long-return-int-conversion
check 1 'int f(long x) { return x; } int main(void) { long x = 1; x <<= 40; return f(x + 42) == 42; }' long-return-narrow-conversion
check 1 'long id(); int main(void) { long x = 1; x <<= 40; return id(x) == x; } long id(long x) { return x; }' long-default-argument
check 1 'long sum(long a, long b, long c, long d, long e, long f, long g, long h) { return a+b+c+d+e+f+g+h; } int main(void) { long x = 1; x <<= 40; return sum(x,x,x,x,x,x,x,x) == x * 8; }' long-eight-arguments
check 1 'long f(int a, char b, long c, unsigned d, unsigned long e, char f, long g, unsigned long h) { return c + g + h - e + a + b + d + f; } int main(void) { long x = 1; x <<= 40; return f(1,2,x,3,x,4,x,x) == x * 2 + 10; }' long-mixed-arguments
check 1 'long f(long x, int n) { if (n) return f(x + 1, n - 1); return x; } int main(void) { long x = 1; x <<= 40; return f(x, 3) == x + 3; }' long-recursion
check 1 'long f(long x) { return x + 1; } int main(void) { long x = 1; x <<= 40; return x + f(f(x)) == x * 2 + 2; }' long-nested-calls

# Long declaration specifiers.
for spec in 'long' 'long int' 'int long' 'unsigned long' 'long unsigned' \
    'unsigned long int' 'unsigned int long' 'long unsigned int' \
    'long int unsigned' 'int unsigned long' 'int long unsigned'; do
    check 42 "$spec f($spec); int main(void) { $spec x; return 42; }" "long-declspecs-$spec"
done
check 42 'long f(long int); long int f(int long); int main(void) { return 42; }' long-compatible-prototypes
check 42 'unsigned long f(long unsigned int); int unsigned long f(unsigned int long); int main(void) { return 42; }' unsigned-long-compatible-prototypes
check 42 'int main(void) { for (long i; 0;) ; return 42; }' long-for-declaration
check 42 'int main(void) { for (unsigned long i; 0;) ; return 42; }' unsigned-long-for-declaration
check 1 'int main(void) { long x; long *p = &x; return (p + 1) - p; }' long-pointer-declaration

check_error long-missing-name 'int main() { int long; return 0; }' '1:22: error: expect variable name'
check_error long-char-specifiers 'int main(void) { long char x; return 0; }' '1:23: error: invalid combination of type specifiers'
check_error char-long-specifiers 'int main(void) { char long x; return 0; }' '1:23: error: invalid combination of type specifiers'
check_error long-long-unsupported 'int main(void) { long long x; return 0; }' '1:23: error: long long is not supported'
check_error long-duplicate-int 'int main(void) { long int int x; return 0; }' '1:27: error: duplicate int type specifier'
check_error long-duplicate-unsigned 'int main(void) { unsigned long unsigned x; return 0; }' '1:32: error: duplicate unsigned type specifier'
check_error long-conflicting-return 'long f(void); int f(void);' '1:19: error: conflicting declaration of function '"'"'f'"'"''
check_error long-conflicting-parameter 'int f(long); int f(int);' '1:18: error: conflicting declaration of function '"'"'f'"'"''
check_error long-conflicting-signedness 'long f(long); unsigned long f(long);' '1:29: error: conflicting declaration of function '"'"'f'"'"''
check_error long-conflicting-parameter-signedness 'int f(long); int f(unsigned long);' '1:18: error: conflicting declaration of function '"'"'f'"'"''
check_error long-incompatible-pointer 'int main(void) { int x; long *p = &x; return 0; }' '1:31: error: incompatible initializer type'
check_error long-declaration-as-statement 'int main(void) { if (1) long x; return 0; }' '1:25: error: expect statement'

# Unsigned char occupies one byte and promotes to int.
check 255 'int main(void) { unsigned char c = -1; return c; }' unsigned-char-load
check 255 'int main(void) { char unsigned c = -1; return c; }' char-unsigned-order
check 1 'int main(void) { unsigned char c = 128; return c > 0 && c == 128; }' unsigned-char-high-bit
check 1 'int main(void) { unsigned char c = 511; return c == 255; }' unsigned-char-truncate
check 1 'int main(void) { unsigned char c = 256; return c == 0; }' unsigned-char-truncate-zero
check 1 'int main(void) { unsigned char c = 0; return (c = -1) == 255 && c == 255; }' unsigned-char-assignment-result
check 1 'int main(void) { unsigned char c = 255; return c + 1 == 256 && c - 256 == -1; }' unsigned-char-integer-promotion
check 1 'int main(void) { unsigned char c = 255; return ~c == -256 && -c == -255; }' unsigned-char-unary-promotion
check 1 'int main(void) { unsigned char c = 255; return c / 2 == 127 && (c >> 1) == 127 && (c << 1) == 510; }' unsigned-char-shift-promotion
check 1 'int main(void) { unsigned char c = 255; return (1 ? c : -1) == 255 && (0 ? c : -1) < 0; }' unsigned-char-conditional-promotion
check 1 'int main(void) { unsigned char c = 255; char s = -1; return c > s && c + s == 254; }' unsigned-char-signed-char-mix
check 1 'int main(void) { unsigned char c = 255; unsigned u = 0; return c < -1 == 0 && c > u && c + u == 255; }' unsigned-char-unsigned-mix
check 1 'int main(void) { unsigned char c = 255; char s = c; unsigned char d = s; return s == -1 && d == 255; }' unsigned-char-signed-conversion
check 1 'int main(void) { unsigned char c = 255; c += 1; c -= 1; c *= 2; return c == 254; }' unsigned-char-compound-truncate
check 1 'int main(void) { unsigned char c = 255; c >>= 1; c /= 2; c %= 7; return c == 0; }' unsigned-char-compound-divmod
check 1 'int main(void) { unsigned char c = 255; return c++ == 255 && c == 0 && --c == 255 && ++c == 0 && c-- == 0 && c == 255; }' unsigned-char-increment
check 1 'int main(void) { unsigned char c = 128; unsigned char *p = &c; return *p == 128 && (*p = -1) == 255 && c == 255; }' unsigned-char-pointer-store
check 1 'int main(void) { unsigned char c = 255; unsigned char *p = &c; return (*p)++ == 255 && c == 0 && --*p == 255; }' unsigned-char-pointer-increment
check 1 'int main(void) { unsigned char c = 255; unsigned char *p = &c; return (p + 1) - p == 1; }' unsigned-char-pointer-size
check 1 'unsigned char id(char unsigned c); char unsigned id(unsigned char c) { return c; } int main(void) { return id(-1) == 255 && id(256) == 0; }' unsigned-char-call-signatures
check 1 'unsigned char f(void) { return -1; } int main(void) { return f() > 0 && f() + 1 == 256; }' unsigned-char-return
check 1 'int f(unsigned char c) { return c == 255; } int main(void) { return f(511); }' unsigned-char-argument-truncate
check 1 'int f(int a, int b, int c, int d, int e, int f, unsigned char g, unsigned char h) { return g == 255 && h == 128; } int main(void) { return f(1, 2, 3, 4, 5, 6, -1, 384); }' unsigned-char-stack-arguments
check 1 'int f(); int main(void) { unsigned char c = 255; return f(c); } int f(int c) { return c == 255; }' unsigned-char-default-promotion
check 42 'int main(void) { unsigned char c = -1; switch (c) { case 255: return 42; case -1: return 1; default: return 0; } }' unsigned-char-switch-promotion
check 1 'int main(void) { int n = 0; for (char unsigned c = 254; c; c++) n++; return n == 2; }' unsigned-char-for-declaration

check_error duplicate-char-specifier 'int main(void) { char char c; return 0; }' '1:23: error: duplicate char type specifier'
check_error unsigned-char-int-specifier 'int main(void) { unsigned char int c; return 0; }' '1:32: error: invalid combination of type specifiers'
check_error int-unsigned-char-specifier 'int main(void) { int unsigned char c; return 0; }' '1:31: error: invalid combination of type specifiers'
check_error unsigned-char-duplicate-unsigned 'int main(void) { unsigned char unsigned c; return 0; }' '1:32: error: duplicate unsigned type specifier'
check_error unsigned-char-conflicting-param 'int f(unsigned char); int f(char);' '1:27: error: conflicting declaration of function '"'"'f'"'"''
check_error unsigned-char-conflicting-return 'unsigned char f(void); char f(void);' '1:29: error: conflicting declaration of function '"'"'f'"'"''
check_error unsigned-char-prototype-promotion 'int f(); int f(unsigned char);' '1:14: error: conflicting declaration of function '"'"'f'"'"''
check_error unsigned-char-prototype-promotion-reverse 'int f(unsigned char); int f();' '1:27: error: conflicting declaration of function '"'"'f'"'"''
check_error unsigned-char-incompatible-pointer 'int main(void) { char c; unsigned char *p = &c; return 0; }' '1:41: error: incompatible initializer type'

# Unsigned arithmetic and the usual arithmetic conversions.
check 1 'int main(void) { unsigned u = -1; return u > 2147483647; }' unsigned-high-bit
check 1 'int main(void) { unsigned u = 0; return -1 > u; }' unsigned-mixed-comparison
check 1 'int main(void) { unsigned u = -1; return u == -1; }' unsigned-mixed-equality
check 1 'int main(void) { unsigned u = 0; return u < -1 && u <= -1 && -1 >= u; }' unsigned-relations
check 0 'int main(void) { unsigned u = -1; return u < 0 || u <= 0 || 0 > u || 0 >= u; }' unsigned-relations-false
check 1 'int main(void) { unsigned u = -1; return u + 1 == 0 && u * u == 1; }' unsigned-wraparound
check 1 'int main(void) { unsigned u = 0; return u - 1 > 0; }' unsigned-subtract-type
check 1 'int main(void) { unsigned u = -1; return u / 2 == 2147483647 && u % 3 == 0; }' unsigned-divmod
check 1 'int main(void) { unsigned u = -1; return u / -1 == 1 && u % -1 == 0; }' unsigned-mixed-divmod
check 1 'int main(void) { unsigned u = -1; return (u >> 1) == 2147483647; }' unsigned-right-shift
check 1 'int main(void) { unsigned n = 1; return (-4 >> n) == -2; }' signed-shift-unsigned-count
check 1 'int main(void) { unsigned u = 1; return (u << 31) > 0; }' unsigned-left-shift
check 1 'int main(void) { unsigned u = 1; return -u > 0 && ~u > 0; }' unsigned-unary-type
check 1 'int main(void) { unsigned u = 0; return (1 ? -1 : u) > 0 && (0 ? u : -1) > 0; }' unsigned-conditional-type
check 1 'int main(void) { unsigned u = 0; char c = -1; return c > u && (c + u) > 0; }' unsigned-char-promotion
check 1 'int main(void) { unsigned u = -1; char c = u; int i = u; return c == -1 && i == -1 && i < 0; }' unsigned-conversion
check 1 'int main(void) { unsigned u = 0; return (u = -1) > 0; }' unsigned-assignment-value
check 1 'int main(void) { unsigned u = -1; u += 1; u -= 1; u *= 2; u /= 2; return u == 2147483647; }' unsigned-compound-arithmetic
check 1 'int main(void) { unsigned u = -1; u >>= 1; u %= 7; return u == 1; }' unsigned-compound-divmod-shift
check 1 'int main(void) { unsigned u = 0; return (u | -1) > 0 && (u ^ -1) > 0 && ((u - 1) & -1) > 0; }' unsigned-bitwise-type
check 1 'int main(void) { unsigned u = -1; return !u == 0 && (u && 1) == 1 && (u || 0) == 1; }' unsigned-logical
check 1 'int main(void) { unsigned u = -1; return (u > 0) - 2 < 0; }' unsigned-comparison-result-int
check 1 'int main(void) { unsigned u = -1; return u++ > 0 && u == 0 && --u > 0 && ++u == 0 && u-- == 0 && u > 0; }' unsigned-increment
check 1 'int main(void) { unsigned u = -1; unsigned *p = &u; *p = *p / 2; return *p == 2147483647; }' unsigned-dereference
check 1 'int main(void) { unsigned u = -1; unsigned *p = &u; return (*p)++ > 0 && u == 0 && --*p > 0; }' unsigned-increment-dereference
check 1 'unsigned id(unsigned u) { return u; } int main(void) { return id(-1) / 2 == 2147483647; }' unsigned-call-return
check 1 'unsigned f(void) { return -1; } int g(int x) { return x < 0; } int main(void) { return g(f()); }' unsigned-return-conversion
check 1 'unsigned f(int a, int b, int c, int d, int e, int f, unsigned g, unsigned h) { return g / h; } int main(void) { return f(1, 2, 3, 4, 5, 6, -1, 2) == 2147483647; }' unsigned-stack-arguments
check 1 'int main(void) { int x = 42; int *p = &x; unsigned u = 1; return *((p + u) - u) == 42; }' unsigned-pointer-offset
check 42 'int main(void) { unsigned u = -1; switch (u) { case -1: return 42; default: return 0; } }' unsigned-switch-negative-case
check 42 'int main(void) { char c = -1; switch (c) { case -1: return 42; default: return 0; } }' char-switch-promotion

check_error unsigned-switch-duplicate 'int main(void) { unsigned u = 0; switch (u) { case -1: return 1; case ~0: return 2; } return 0; }' '1:66: error: duplicate case value'
check_error switch-pointer 'int main(void) { int x; switch (&x) { default: return 0; } }' '1:33: error: switch condition must have integer type'
check_error case-overflow-mod 'int main(void) { switch (0) { case (-2147483647 - 1) % -1: return 1; } return 0; }' '1:58: error: integer overflow in constant expression'

# Both orders of the unsigned int declaration specifiers denote one type.
check 42 'int main(void) { unsigned int x = 42; return x; }' unsigned-int-local
check 42 'int main(void) { int unsigned x = 42; return x; }' int-unsigned-local
check 42 'unsigned int id(int unsigned x); int unsigned id(unsigned int x) { return x; } int main(void) { return id(42); }' unsigned-specifier-signatures
check 42 'unsigned int *id(int unsigned *p) { return p; } int main(void) { unsigned int x = 42; int unsigned *p = &x; return *id(p); }' unsigned-specifier-pointers
check 42 'int main(void) { int n = 0; for (int unsigned i = 0; i < 6; i++) n += 7; return n; }' unsigned-specifier-for
check_error unsigned-missing-name 'int main() { int unsigned; return 0; }' '1:26: error: expect variable name'
check_error duplicate-int-specifier 'int main(void) { int int x; return 0; }' '1:22: error: duplicate int type specifier'
check_error duplicate-unsigned-specifier 'int main(void) { unsigned unsigned x; return 0; }' '1:27: error: duplicate unsigned type specifier'
check_error duplicate-int-after-unsigned 'int main(void) { int unsigned int x; return 0; }' '1:31: error: duplicate int type specifier'
check_error duplicate-unsigned-after-int 'int main(void) { unsigned int unsigned x; return 0; }' '1:31: error: duplicate unsigned type specifier'
check_error invalid-unsigned-void 'int main(void) { unsigned void x; return 0; }' '1:27: error: expect variable name'

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
check_error unexpected-mul 'int main() { return * 2; }' '1:21: error: invalid deref target'
check_error double-operator 'int main() { return 1 + * 2; }' '1:25: error: invalid deref target'
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
check_error invalid-int-char-specifiers 'int main() { int char; return 0; }' '1:18: error: invalid combination of type specifiers'
for kw in \
    _Alignas _Alignof _Atomic _Bool _Complex _Generic _Imaginary \
	     _Noreturn _Static_assert _Thread_local \
	     auto break case const continue default do double else enum extern \
	     float for goto if inline register restrict signed sizeof \
	     static struct switch typedef union void volatile while; do
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
	    '1:21: error: invalid address-of target'
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
check_error break-outside-loop 'int main() { break; }' \
	    '1:14: error: break statement not within loop or switch'
check_error continue-outside-loop 'int main() { continue; }' \
	    '1:14: error: continue statement not within loop'
check_error break-after-loop 'int main() { while (1) break; break; }' \
	    '1:31: error: break statement not within loop or switch'
check_error case-outside-switch 'int main() { case 1: ; }' \
	    '1:14: error: case label not within a switch statement'
check_error default-outside-switch 'int main() { default: ; }' \
	    '1:14: error: default label not within a switch statement'
check_error duplicate-case 'int main() { switch (1) { case 1: ; case 1: ; } }' \
	    '1:37: error: duplicate case value'
check_error duplicate-default \
	    'int main() { switch (1) { default: ; default: ; } }' \
	    '1:38: error: multiple default labels in one switch'
check_error case-not-constant \
	    'int main() { int a; switch (1) { case a: ; } }' \
	    '1:40: error: case label is not an integer constant'
check_error decl-as-if-body 'int main() { if (1) int a = 1; return 0; }' \
	    '1:21: error: expect statement'
check_error decl-as-else-body 'int main() { if (1) ; else int b = 2; }' \
	    '1:28: error: expect statement'
check_error decl-as-while-body 'int main() { while (1) int a; }' \
	    '1:24: error: expect statement'
check_error decl-as-for-body 'int main() { for (;;) int a; }' \
	    '1:23: error: expect statement'
check_error decl-as-do-body 'int main() { do int a; while (1); }' \
	    '1:17: error: expect statement'
check_error decl-as-label-body 'int main() { L: int a; }' \
	    '1:17: error: expect statement'
check_error decl-as-case-body \
	    'int main() { switch (1) { case 1: int a; } }' \
	    '1:35: error: expect statement'
check_error redefinition \
	    'int foo() { return 0; } int foo() { return 1; } int main() { return 0; }' \
	    "1:29: error: redefinition of function 'foo'"
check_error call-undefined 'int main() { return foo(); }' \
	    "1:21: error: function 'foo' not declared"
check_error call-on-variable 'int main() { int a = 1; return a(); }' \
	    '1:32: error: called object is not a function'
check_error param-shadow 'int foo(int a) { int a = 42; return a; } int main() { return foo(24); }' \
	    "1:22: error: declaration of 'a' shadows a parameter"
check_error too-many-args 'int foo(int a) { return a; } int main() { return foo(1, 2); }' \
	    "1:50: error: wrong number of arguments to function 'foo'"
check_error too-few-args 'int foo(int a) { return a; } int main() { return foo(); }' \
	    "1:50: error: wrong number of arguments to function 'foo'"
check_error missing-conditional-colon 'int main() { return 1 ? 2; }' \
	    "1:26: error: expect ':'"
check_error missing-conditional-then 'int main() { return 1 ? : 2; }' \
	    '1:25: error: expect expr'
check_error missing-conditional-else 'int main() { return 1 ? 2 : ; }' \
	    '1:29: error: expect expr'
check_error question-operand 'int main() { return ? 1; }' \
	    '1:21: error: expect expr'
check_error comma-assign 'int main() { int a; (a, a) = 1; return 0; }' \
	    '1:28: error: invalid assignment target'
check_error case-comma 'int main() { switch (1) { case 1, 2: ; } }' \
	    "1:33: error: expect ':'"
check_error missing-break-semi 'int main() { while (1) break }' \
	    "1:30: error: expect ';'"
check_error goto-undefined 'int main() { goto L; }' \
	    "1:14: error: label 'L' not defined"
check_error duplicate-label 'int main() { L: ; L: ; }' \
	    '1:19: error: duplicate label'
check_error duplicate-label-scope 'int main() { { L: ; } L: ; }' \
	    '1:23: error: duplicate label'
check_error goto-missing-name 'int main() { goto 1; }' \
	    '1:19: error: expect label name'
check_error goto-missing-semi 'int main() { L: ; goto L }' \
	    "1:26: error: expect ';'"
check_error octal-invalid-eight 'int main() { return 08; }' '1:21: error: invalid digit in octal integer literal'
check_error case-shift-negative 'int main() { switch (0) { case 1 << -1: return 1; default: return 0; } }' '1:39: error: invalid shift count in constant expression'
check_error case-shift-32 'int main() { switch (0) { case 1 << 32: return 1; default: return 0; } }' '1:39: error: invalid shift count in constant expression'
check_error case-overflow-mul 'int main() { switch (0) { case 2147483647 * 2147483647: return 1; default: return 0; } }' '1:55: error: integer overflow in constant expression'
check_error function-no-declaration 'int main() { return missing(); } int missing() { return 1; }' "1:21: error: function 'missing' not declared"
check_error conflicting-prototypes 'int f(int); int f(int x, int y); int main() { return 0; }' "1:17: error: conflicting declaration of function 'f'"
check_error void-with-arg 'int f(void); int main(void) { return f(1); } int f(void) { return 42; }' "1:38: error: wrong number of arguments to function 'f'"
check_error ptr-plus-ptr \
	    'int main() { int a; int *p; int *q; p = &a; q = &a; return p + q; }' \
	    '1:62: error: invalid operands to binary +'
check_error int-minus-ptr 'int main() { int a; int *p; p = &a; return 1 - p; }' \
	    '1:46: error: invalid operands to binary -'

printf 'All %d tests passed (%d execution, %d rejection).\n' "$((passed + rejected))" "$passed" "$rejected"
