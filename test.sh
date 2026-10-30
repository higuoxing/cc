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

# Global pointer addresses are emitted as relocations with byte offsets.
check 42 'int x = 42; int *p = &x; int main(void) { return *p; }' global-pointer-integer-address
check 1 'char x = -1; char *p = &x; int main(void) { return *p == -1; }' global-pointer-char-address
check 1 'unsigned char x = 255; unsigned char *p = &x; int main(void) { return *p == 255; }' global-pointer-unsigned-char-address
check 1 'short x = -32768; short *p = &x; int main(void) { return *p == -32768; }' global-pointer-short-address
check 1 'unsigned short x = 65535; unsigned short *p = &x; int main(void) { return *p == 65535; }' global-pointer-unsigned-short-address
check 1 'unsigned x = 4294967295U; unsigned *p = &x; int main(void) { return *p == 4294967295U; }' global-pointer-unsigned-address
check 1 'long x = 1099511627776L; long *p = &x; int main(void) { return *p == 1099511627776L; }' global-pointer-long-address
check 1 'unsigned long long x = 18446744073709551615ULL; unsigned long long *p = &x; int main(void) { return *p == 18446744073709551615ULL; }' global-pointer-unsigned-long-long-address
check 42 'enum E { ANSWER = 42 }; enum E x = ANSWER; enum E *p = &x; int main(void) { return *p; }' global-pointer-enum-address
check 42 'int x = 42; void *p = &x; int main(void) { int *q = p; return *q; }' global-pointer-void-address
check 42 'int x = 42, *p = &x, **q = &p; int main(void) { return **q; }' global-pointer-pointer-object-address
check 1 'void *p = &p; int main(void) { return p == &p; }' global-pointer-self-address
check 42 'typedef int I; I x = 42; I *p = &x; int main(void) { return *p; }' global-pointer-typedef-address
check 42 'int a[4]; int *p = a; int main(void) { a[0] = 42; return *p; }' global-pointer-array-decay-initializer
check 42 'int a[4]; int *p = &a[2]; int main(void) { *p = 42; return a[2]; }' global-pointer-array-element-address
check 42 'int a[4]; int *p = 2 + a; int main(void) { a[2] = 42; return *p; }' global-pointer-reversed-add-initializer
check 4 'int a[4]; int *p = a + 4; int main(void) { return p - a; }' global-pointer-one-past-initializer
check 42 'int a[4]; int *p = a + 3 - 2; int main(void) { a[1] = 42; return *p; }' global-pointer-folded-offset-initializer
check 42 'int a[4]; int *p = &2[a]; int main(void) { a[2] = 42; return *p; }' global-pointer-reversed-subscript-initializer
check 42 'int a[4]; int *p = &*(a + 2); int main(void) { a[2] = 42; return *p; }' global-pointer-address-deref-initializer
check 42 'char a[4]; char *p = a + 2; int main(void) { a[2] = 42; return *p; }' global-pointer-byte-offset-initializer
check 42 'short a[4]; short *p = a + 2; int main(void) { a[2] = 42; return *p; }' global-pointer-short-offset-initializer
check 1 'long a[4]; long *p = a + 2; int main(void) { a[2] = 1099511627776L; return *p == 1099511627776L; }' global-pointer-long-offset-initializer
check 42 'struct S { char c; long n; }; struct S s; long *p = &s.n; int main(void) { s.n = 42; return *p; }' global-pointer-member-address
check 42 'struct S { char c; long n; }; struct S s; long *p = &(&s)->n; int main(void) { s.n = 42; return *p; }' global-pointer-arrow-member-address
check 42 'struct I { char c; short n; }; struct O { char c; struct I i; }; struct O s; short *p = &s.i.n; int main(void) { s.i.n = 42; return *p; }' global-pointer-nested-member-address
check 42 'struct S { char c; int a[3]; }; struct S s; int *p = s.a + 2; int main(void) { s.a[2] = 42; return *p; }' global-pointer-array-member-decay
check 42 'struct S { char c; long n; }; struct S a[3]; long *p = &a[2].n; int main(void) { a[2].n = 42; return *p; }' global-pointer-struct-array-member-address
check 42 'struct S; struct S s; struct S *p = &s; struct S { int n; }; int main(void) { p->n = 42; return s.n; }' global-pointer-completed-struct-address
check 101 'char *p = "hello"; int main(void) { return p[1]; }' global-pointer-string-initializer
check 0 'char *p = ""; int main(void) { return p[0]; }' global-pointer-empty-string-initializer
check 1 'char *p = "he" "llo"; int main(void) { return p[4] == 111 && p[5] == 0; }' global-pointer-concatenated-string-initializer
check 1 'char *p = "A\0B"; int main(void) { return p[0] == 65 && p[1] == 0 && p[2] == 66 && p[3] == 0; }' global-pointer-embedded-zero-string-initializer
check 1 'char *p = "\377B"; int main(void) { return p[0] == -1 && p[1] == 66 && p[2] == 0; }' global-pointer-escaped-byte-string-initializer
check 1 'char *p = "123456789abcdef"; int main(void) { return p[14] == 102 && p[15] == 0; }' global-pointer-string-full-byte-line
check 1 'char *p = "123456789abcdefg"; int main(void) { return p[15] == 103 && p[16] == 0; }' global-pointer-string-next-byte-line
check 1 'char *p = "123456789abcdefg123456789abcdefgZ"; int main(void) { return p[32] == 90 && p[33] == 0; }' global-pointer-string-multiple-byte-lines
check 108 'char *p = "hello" + 2; int main(void) { return *p; }' global-pointer-string-offset-initializer
check 111 'char *p = &"hello"[4]; int main(void) { return *p; }' global-pointer-string-subscript-initializer
check 65 'void *p = "A"; int main(void) { char *q = p; return *q; }' global-pointer-void-string-initializer
check 133 'char *p = "AB", *q = "CD"; int main(void) { return p[0] + q[1]; }' global-pointer-multiple-string-initializers
check 42 'int x = 42; int *p = 1 ? &x : 0; int main(void) { return *p; }' global-pointer-conditional-address-initializer
check 42 'int *f(void); int x = 42; int *p = 1 ? &x : f(); int main(void) { return *p; }' global-pointer-unevaluated-call-address-initializer
check 42 'int x = 42; int *p; int *p = &x; int *p; int main(void) { return *p; }' global-pointer-address-tentative-definition
check 42 'int x = 42; int *p; int *f(void) { return p; } int *p = &x; int main(void) { return *f(); }' global-pointer-initializer-after-function-reference
check 42 'int x; int *p = &x; int x = 42; int main(void) { return *p; }' global-pointer-later-object-definition
check 42 'int x = 10, y = 42; int *p = &x; int main(void) { p = &y; return *p; }' global-pointer-initialized-pointer-writable
check 42 'int x = 0; int *p = &x; int main(void) { *p = 42; return x; }' global-pointer-initialized-target-writable
check 42 'char *p = "A"; int z; int f(void) { char *q = "B"; return q[0] - p[0]; } char *r = "C"; int x = 40; int main(void) { return x + z + f() + r[0] - 66; }' global-pointer-data-bss-rodata-text-sections

# Null pointer initializers and constant-expression diagnostics.
check 1 'int *p = 0; int main(void) { return p == 0; }' global-pointer-null-initializer
check 1 'void *p = 0; int main(void) { return p == 0; }' global-void-pointer-null-initializer
check 1 'int **p = 0; int main(void) { return p == 0; }' global-pointer-to-pointer-null-initializer
check 1 'struct S; struct S *p = 0; int main(void) { return p == 0; }' global-incomplete-pointer-null-initializer
check 1 'int *p = 1 - 1; int main(void) { return p == 0; }' global-pointer-constant-null-initializer
check 1 'int *p = 0ULL; int main(void) { return p == 0; }' global-pointer-unsigned-null-initializer
check 1 'enum { ZERO }; int *p = ZERO; int main(void) { return p == 0; }' global-pointer-enum-null-initializer
check 1 'typedef int *P; P p = 0; int main(void) { return p == 0; }' global-pointer-typedef-null-initializer
check 1 'int *p; int *p = 0; int *p; int main(void) { return p == 0; }' global-pointer-null-tentative-definition
check 1 'int x; int *p = 0 ? &x : 0; int main(void) { return p == 0; }' global-pointer-null-conditional-initializer
check 1 'int *f(void); int *p = 0 ? f() : 0; int main(void) { return p == 0; }' global-pointer-null-unevaluated-call
check 1 'int x; int *p = &*(0 ? &x : 0); int main(void) { return p == 0; }' global-pointer-null-address-deref
check 42 'int x = 42; int *p = 0; int main(void) { p = &x; return *p; }' global-pointer-null-then-store
check_error global-initializer-nonzero-integer 'int *p = 1;' "1:6: error: incompatible initializer type"
check_error global-initializer-incompatible-pointer 'long x; int *p = &x;' "1:14: error: incompatible initializer type"
check_error global-initializer-incompatible-string 'unsigned char *p = "A";' "1:16: error: incompatible initializer type"
check_error global-initializer-pointer-read 'int *q; int *p = q;' "1:18: error: global initializer is not a constant expression"
check_error global-initializer-initialized-pointer-read 'int x; int *q = &x; int *p = q;' "1:30: error: global initializer is not a constant expression"
check_error global-initializer-null-pointer-read 'int *q = 0; int *p = q;' "1:22: error: global initializer is not a constant expression"
check_error global-initializer-pointer-self-read 'int *p = p;' "1:10: error: global initializer is not a constant expression"
check_error global-initializer-pointer-call 'int *f(void); int *p = f();' "1:24: error: global initializer is not a constant expression"
check_error global-initializer-pointer-dereference 'int **q; int *p = *q;' "1:19: error: global initializer is not a constant expression"
check_error global-initializer-pointer-member-read 'struct S { int *p; }; struct S s; int *p = s.p;' "1:45: error: global initializer is not a constant expression"
check_error global-initializer-pointer-arrow-read 'struct S { int n; }; struct S *q; int *p = &q->n;' "1:44: error: global initializer is not a constant expression"
check_error global-initializer-dynamic-offset 'int a[4], n; int *p = a + n;' "1:25: error: global initializer is not a constant expression"
check_error global-initializer-dynamic-subscript 'int a[4], n; int *p = &a[n];' "1:23: error: global initializer is not a constant expression"
check_error global-initializer-offset-call 'int f(void); int a[4]; int *p = a + f();' "1:35: error: global initializer is not a constant expression"
check_error global-initializer-pointer-assignment 'int x, *q; int *p = (q = &x);' "1:24: error: global initializer is not a constant expression"
check_error global-initializer-pointer-increment 'int *q; int *p = q++;' "1:19: error: global initializer is not a constant expression"
check_error global-initializer-pointer-comma 'int x; int *p = (0, &x);' "1:19: error: global initializer is not a constant expression"
check_error global-initializer-dynamic-condition 'int n, x; int *p = n ? &x : 0;' "1:22: error: global initializer is not a constant expression"
check_error global-initializer-evaluated-pointer-call 'int *f(void); int x; int *p = 1 ? f() : &x;' "1:33: error: global initializer is not a constant expression"
check_error global-initializer-pointer-zero-redefinition 'int *p = 0; int *p = 0;' "1:18: error: redefinition of variable 'p'"
check_error global-initializer-pointer-address-redefinition 'int x; int *p = &x; int *p = &x;' "1:26: error: redefinition of variable 'p'"
check_error global-initializer-pointer-scaled-offset-overflow 'long a[2]; long *p = a + 9223372036854775807L;' "1:24: error: address offset exceeds supported range"
check_error global-initializer-pointer-unsigned-offset-overflow 'char a[2]; char *p = a + 18446744073709551615ULL;' "1:24: error: address offset exceeds supported range"
check_error global-initializer-pointer-total-offset-overflow 'char a[2]; char *p = a + 9223372036854775807L + 1;' "1:47: error: address offset exceeds supported range"
check_error global-initializer-pointer-sub-offset-overflow 'char a[2]; char *p = a - (-9223372036854775807L - 1);' "1:24: error: address offset exceeds supported range"
check_error global-initializer-pointer-negative-offset-overflow 'long a[2]; long *p = a + (-9223372036854775807L - 1);' "1:24: error: address offset exceeds supported range"
check_error global-initializer-pointer-offset-division-zero 'int a[2]; int *p = a + (1 / 0);' "1:31: error: division by zero in constant expression"

# Scalar global initializers use the converted value at the object's width.
check 42 'int x = 42; int main(void) { return x; }' global-int-initializer
check 1 'char x = -1; int main(void) { return x == -1; }' global-char-negative-initializer
check 1 'unsigned char x = 255; int main(void) { return x == 255; }' global-unsigned-char-initializer
check 1 'char x = 511; int main(void) { return x == -1; }' global-char-narrow-initializer
check 1 'unsigned char x = -1; int main(void) { return x == 255; }' global-unsigned-char-narrow-initializer
check 1 'short x = -32768; int main(void) { return x == -32768; }' global-short-negative-initializer
check 1 'unsigned short x = 65535; int main(void) { return x == 65535; }' global-unsigned-short-initializer
check 1 'short x = 65535; int main(void) { return x == -1; }' global-short-narrow-initializer
check 1 'unsigned short x = -1; int main(void) { return x == 65535; }' global-unsigned-short-narrow-initializer
check 1 'int x = -2147483647 - 1; int main(void) { return x == -2147483647 - 1; }' global-int-min-initializer
check 1 'unsigned x = 4294967295U; int main(void) { return x == 4294967295U; }' global-unsigned-initializer
check 1 'int x = 4294967295U; int main(void) { return x == -1; }' global-int-narrow-initializer
check 1 'unsigned x = -1; int main(void) { return x == 4294967295U; }' global-unsigned-negative-initializer
check 1 'long x = 1099511627776L; int main(void) { return x == 1099511627776L; }' global-long-initializer
check 1 'long x = -1099511627776L; int main(void) { return x == -1099511627776L; }' global-long-negative-initializer
check 1 'unsigned long x = 18446744073709551615UL; int main(void) { return x == 18446744073709551615UL; }' global-unsigned-long-initializer
check 1 'long long x = -9223372036854775807LL - 1; int main(void) { return x == -9223372036854775807LL - 1; }' global-long-long-min-initializer
check 1 'unsigned long long x = 18446744073709551615ULL; int main(void) { return x == 18446744073709551615ULL; }' global-unsigned-long-long-initializer
check 42 'enum E { ANSWER = 42 }; enum E x = ANSWER; int main(void) { return x; }' global-enum-initializer
check 1 'enum E { NEG = -1 }; enum E x = NEG; int main(void) { return x == -1; }' global-enum-negative-initializer
check 42 'typedef unsigned long U; U x = 42; int main(void) { return x; }' global-typedef-initializer
check 42 'int x = 6 * 7; int main(void) { return x; }' global-constant-expression-initializer
check 8 'unsigned long x = sizeof(long); int main(void) { return x; }' global-sizeof-initializer
check 65 'int x = '\''A'\''; int main(void) { return x; }' global-character-initializer
check 42 'int x = 1 ? 42 : 1 / 0; int main(void) { return x; }' global-conditional-initializer
check 42 'int f(void); int x = 1 ? 42 : f(); int main(void) { return x; }' global-unevaluated-call-initializer
check 42 'int x; int x = 42; int x; int main(void) { return x; }' global-nonzero-tentative-definition
check 42 'int x = 42; int x; int main(void) { return x; }' global-nonzero-followed-by-tentative
check 42 'int x; int f(void) { return x; } int x = 42; int main(void) { return f(); }' global-initializer-after-reference
check 42 'int x = 20, y = 22; int main(void) { return x + y; }' global-multiple-initializers
check 42 'int x = 20; int main(void) { x += 22; return x; }' global-initialized-object-writable
check 42 'int x = 21; int main(void) { int *p = &x; *p *= 2; return x; }' global-initialized-object-address
check 1 'char a = 1, b = 2; short c = 3, d = 4; int main(void) { a = 7; c = 8; return a == 7 && b == 2 && c == 8 && d == 4; }' global-initialized-neighbor-storage
check 42 'int a = 10, z, b = 12; int f(void) { char *p = "A"; return p[0] - 45; } int c = 0; int main(void) { return a + z + b + f() + c; }' global-data-bss-text-rodata-sections
check 0 'int x = 0; int main(void) { return x; }' global-zero-initializer
check 0 'int x; int x = 0; int x; int main(void) { return x; }' global-zero-tentative-definition
check 0 'int x = 0, y = 0; int main(void) { return x + y; }' global-zero-multiple-definitions
check 0 'int f(void); int x = 0 && f(); int main(void) { return x; }' global-zero-unevaluated-call
check 0 'unsigned char x = 256; int main(void) { return x; }' global-zero-after-narrowing
check_error global-initializer-redefinition 'int x = 1; int x = 2;' "1:16: error: redefinition of variable 'x'"
check_error global-initializer-zero-redefinition 'int x = 0; int x = 0;' "1:16: error: redefinition of variable 'x'"
check_error global-initializer-tentative-redefinition 'int x; int x = 0; int x = 1;' "1:23: error: redefinition of variable 'x'"
check_error global-initializer-comma-redefinition 'int x = 1, x = 2;' "1:12: error: redefinition of variable 'x'"
check_error global-initializer-conflicting-definition 'int x = 1; long x = 2;' "1:17: error: conflicting declaration of variable 'x'"
check_error global-initializer-variable-read 'int y; int x = y;' "1:16: error: global initializer is not a constant expression"
check_error global-initializer-initialized-variable-read 'int y = 1; int x = y;' "1:20: error: global initializer is not a constant expression"
check_error global-initializer-self-read 'int x = x;' "1:9: error: global initializer is not a constant expression"
check_error global-initializer-function-call 'int f(void); int x = f();' "1:22: error: global initializer is not a constant expression"
check_error global-initializer-assignment 'int y; int x = (y = 1);' "1:19: error: global initializer is not a constant expression"
check_error global-initializer-comma-expression 'int x = (1, 42);' "1:11: error: global initializer is not a constant expression"
check_error global-initializer-increment 'int y; int x = y++;' "1:17: error: global initializer is not a constant expression"
check_error global-initializer-evaluated-call 'int f(void); int x = 1 && f();' "1:24: error: global initializer is not a constant expression"
check_error global-initializer-incompatible-string 'int x = "A";' "1:5: error: incompatible initializer type"
check_error global-initializer-void-call 'void f(void); int x = f();' "1:23: error: void expression used as a value"
check_error global-initializer-array-unsupported 'int a[2] = {1,2};' "1:10: error: array initializers are not supported yet"
check_error global-initializer-struct-unsupported 'struct S { int x; }; struct S s = {1};' "1:33: error: struct initializers are not supported yet"
check_error global-initializer-division-by-zero 'int x = 1 / 0;' "1:14: error: division by zero in constant expression"
check_error global-initializer-signed-overflow 'int x = 2147483647 + 1;' "1:23: error: integer overflow in constant expression"
check_error global-initializer-invalid-shift 'int x = 1 << 32;' "1:16: error: invalid shift count in constant expression"
check_error global-initializer-missing-expression 'int x = ;' "1:9: error: expect expr"

# Global objects use zero-filled storage and RIP-relative addresses.
check 1 'char value; int main(void) { if (value != 0) return 0; value = -1; return value == -1; }' global-char-zero-and-store
check 1 'unsigned char value; int main(void) { if (value != 0) return 0; value = 255; return value == 255; }' global-unsigned-char-zero-and-store
check 1 'short value; int main(void) { if (value != 0) return 0; value = -1; return value == -1; }' global-short-zero-and-store
check 1 'unsigned short value; int main(void) { if (value != 0) return 0; value = 65535; return value == 65535; }' global-unsigned-short-zero-and-store
check 1 'int value; int main(void) { if (value != 0) return 0; value = -1; return value == -1; }' global-int-zero-and-store
check 1 'unsigned value; int main(void) { if (value != 0) return 0; value = 4294967295U; return value == 4294967295U; }' global-unsigned-zero-and-store
check 1 'long value; int main(void) { if (value != 0) return 0; value = 1099511627776L; return value == 1099511627776L; }' global-long-zero-and-store
check 1 'unsigned long value; int main(void) { if (value != 0) return 0; value = 18446744073709551615UL; return value == 18446744073709551615UL; }' global-unsigned-long-zero-and-store
check 1 'long long value; int main(void) { if (value != 0) return 0; value = -9223372036854775807LL; return value == -9223372036854775807LL; }' global-long-long-zero-and-store
check 1 'unsigned long long value; int main(void) { if (value != 0) return 0; value = 18446744073709551615ULL; return value == 18446744073709551615ULL; }' global-unsigned-long-long-zero-and-store
check 42 'int x; int main(void) { x = 42; return x; }' global-assignment
check 1 'int *p; int main(void) { return p == 0; }' global-pointer-zero
check 42 'int x, *p; int main(void) { p = &x; *p = 42; return x; }' global-pointer-store
check 42 'int x, *p, **pp; int main(void) { pp = &p; p = &x; **pp = 42; return x; }' global-pointer-to-pointer
check 42 'int a[3]; int main(void) { if (a[0] || a[1] || a[2]) return 0; a[2] = 42; return a[2]; }' global-array-zero-and-store
check 42 'char a[5]; int main(void) { a[0] = 1; a[4] = 42; if (a[1] || a[2] || a[3]) return 0; return a[4]; }' global-char-array
check 1 'unsigned short a[3]; int main(void) { a[1] = 65535; return a[0] == 0 && a[1] == 65535 && a[2] == 0; }' global-short-array
check 1 'long a[3]; int main(void) { a[1] = 1099511627776L; return a[0] == 0 && a[1] == 1099511627776L && a[2] == 0; }' global-long-array
check 3 'int a[3]; int main(void) { return (a + 3) - a; }' global-pointer-difference
check 42 'int a[3], *p; int main(void) { p = a; p++; *p = 42; return a[1]; }' global-pointer-increment
check 42 'struct S { char c; long value; }; struct S s; int main(void) { if (s.c || s.value) return 0; s.c = 7; s.value = 42; return s.value; }' global-struct-members
check 42 'struct S { int a[3]; }; struct S s; int main(void) { s.a[2] = 42; return s.a[2]; }' global-struct-array-member
check 42 'struct I { short x; }; struct O { char c; struct I i; long n; }; struct O o; int main(void) { o.i.x = 42; o.n = 1099511627776L; if (o.n != 1099511627776L) return 0; return o.i.x; }' global-nested-struct
check 42 'struct S s; struct S { int value; struct S *next; }; int main(void) { s.value = 42; s.next = &s; return s.next->value; }' global-completed-struct
check 1 'struct S { char c; long n; }; struct S s; int main(void) { void *raw = &s; unsigned char *bytes = raw; unsigned long i; for (i = 0; i < sizeof(s); i++) if (bytes[i] != 0) return 0; return 1; }' global-struct-storage-zero
check 42 'enum E { ANSWER = 42 }; enum E e; int main(void) { if (e != 0) return 0; e = ANSWER; return e; }' global-enum
check 42 'typedef unsigned long Counter; Counter count; int main(void) { count = 42; return count; }' global-typedef
check 42 'typedef int A[3]; A a; int main(void) { a[2] = 42; return a[2]; }' global-typedef-array
check 42 'int x; int x; int main(void) { x = 42; return x; }' global-repeated-declaration
check 42 'int a, b, a, c; int main(void) { a = 10; b = 12; c = 20; return a + b + c; }' global-multiple-and-repeated
check 42 'int a; int a, b; int main(void) { a = 20; b = 22; return a + b; }' global-new-after-repeated
check 42 'typedef int I; I x; int x; int main(void) { x = 42; return x; }' global-compatible-alias-redeclaration
check 42 'int x; int f(void) { x = 42; return 0; } int main(void) { f(); return x; }' global-cross-function-store
check 42 'int count; int f(int n) { if (n == 0) return count; count++; return f(n - 1); } int main(void) { return f(42); }' global-recursive-state
check 42 'int x; int f(int x) { x = 7; return x; } int main(void) { x = 42; f(1); return x; }' global-parameter-shadow
check 42 'int x; int main(void) { x = 42; { int x = 7; if (x != 7) return 0; } return x; }' global-block-shadow
check 42 'int x; int main(void) { x = 42; for (int x = 0; x < 3; x++) {} return x; }' global-for-shadow
check 42 'int f(void) { return 20; } int x; int main(void) { x = 22; return f() + x; }' global-object-after-function
check 98 'int f(void) { char *p = "abc"; return p[1]; } int x; int main(void) { x = f(); return x; }' global-sections-after-string
check 65 'char *p; int main(void) { p = "A"; return p[0]; }' global-pointer-to-string
check 42 'int a[3]; int *f(void) { return a; } int main(void) { f()[1] = 42; return a[1]; }' global-return-array-pointer
check 42 'struct S { int x; }; struct S s; struct S *f(void) { return &s; } int main(void) { f()->x = 42; return s.x; }' global-return-struct-pointer
check 42 'int x; int f(int a,int b,int c,int d,int e,int f,int g,int *p) { *p = a+b+c+d+e+f+g; return 0; } int main(void) { f(1,2,3,4,5,6,21,&x); return x; }' global-eighth-pointer-argument
check 42 'int x; int main(void) { x = 20; x += 22; return x; }' global-compound-assignment
check 42 'int x; int main(void) { x = 40; ++x; x++; return x; }' global-increment
check 1 'unsigned char c; int main(void) { c = 255; c++; return c == 0; }' global-narrow-increment
check 1 'char a, b; int main(void) { b = 7; a = 1; return a == 1 && b == 7; }' global-byte-store-neighbor
check 1 'short a, b; int main(void) { b = 7; a = 1; return a == 1 && b == 7; }' global-short-store-neighbor
check 12 'int a[3]; int main(void) { return sizeof(a); }' global-sizeof-array

# File-scope object declaration diagnostics.
check_error global-void 'void x;' "1:6: error: variable has void type"
check_error global-void-alias 'typedef void V; V x;' "1:19: error: variable has void type"
check_error global-conflicting-type 'int x; long x;' "1:13: error: conflicting declaration of variable 'x'"
check_error global-conflicting-array 'int a[2]; int a[3];' "1:15: error: conflicting declaration of variable 'a'"
check_error global-conflicting-pointer 'int *p; char *p;' "1:15: error: conflicting declaration of variable 'p'"
check_error global-function-conflict 'int f; int f(void);' "1:12: error: function conflicts with variable 'f'"
check_error global-variable-conflict 'int f(void); int f;' "1:18: error: variable conflicts with function 'f'"
check_error global-typedef-conflict 'typedef int T; int T;' "1:20: error: variable conflicts with typedef 'T'"
check_error global-typedef-name-conflict 'int T; typedef int T;' "1:20: error: typedef conflicts with variable 'T'"
check_error global-enumerator-conflict 'enum { X }; int X;' "1:17: error: variable conflicts with enumerator 'X'"
check_error global-enumerator-name-conflict 'int X; enum { X };' "1:15: error: enumerator conflicts with variable 'X'"
check_error global-incomplete 'struct S x;' "1:10: error: variable has incomplete type"
check_error global-repeated-incomplete 'struct S x; struct S x;' "1:10: error: variable has incomplete type"
check_error global-array-initializer-unsupported 'int a[3] = {0};' "1:10: error: array initializers are not supported yet"
check_error global-missing-name 'int;' "1:4: error: expect declaration name"
check_error global-missing-name-after-comma 'int a,;' "1:7: error: expect variable name"
check_error global-mixed-function-unsupported 'int a, f(void);' "1:9: error: function declarations in a declaration list are not supported yet"
check_error global-distinct-structs 'struct A { int x; }; struct B { int x; }; struct A a; struct B a;' "1:64: error: conflicting declaration of variable 'a'"
check_error global-use-before-declaration 'int f(void) { return x; } int x;' "1:22: error: undeclared identifier"
check_error global-nonconstant-array-bound 'int n; int a[n];' "1:15: error: not an integer constant expression"

# Declaration specifiers use one bitmask for built-in and named types.
check 42 'typedef int T; int main(void) { unsigned T = 42; return T; }' declspec-unsigned-shadows-typedef
check 42 'typedef struct { int x; } S; int main(void) { S S; S.x = 42; return S.x; }' declspec-struct-alias-as-declarator
check 42 'typedef enum { A = 42 } E; int main(void) { E E = A; return E; }' declspec-enum-alias-as-declarator
check_error declspec-unsigned-struct 'int main(void) { unsigned struct S x; }' "1:27: error: invalid combination of type specifiers"
check_error declspec-struct-unsigned 'int main(void) { struct S unsigned x; }' "1:27: error: invalid combination of type specifiers"
check_error declspec-unsigned-enum 'int main(void) { unsigned enum E x; }' "1:27: error: invalid combination of type specifiers"
check_error declspec-enum-unsigned 'enum E { A }; int main(void) { enum E unsigned x; }' "1:39: error: invalid combination of type specifiers"
check_error declspec-void-int 'int main(void) { void int x; }' "1:23: error: invalid combination of type specifiers"
check_error declspec-int-void 'int main(void) { int void x; }' "1:22: error: invalid combination of type specifiers"
check_error declspec-alias-unsigned 'typedef int T; int main(void) { T unsigned x; }' "1:35: error: invalid combination of type specifiers"
check_error declspec-duplicate-void 'typedef void void V;' "1:14: error: duplicate void type specifier"
check_error declspec-struct-enum 'struct S { int x; }; struct S enum E x;' "1:31: error: invalid combination of type specifiers"

# Ordinary identifier bindings preserve shadowing and file-scope functions.
check 42 'int f(int f); int main(void) { return f(42); } int f(int f) { return f; }' symbol-function-binding-below-parameter
check 42 'int X(void) { return 42; } int main(void) { { enum { X = 7 }; { typedef char X; { int X = 9; if (X != 9) return 0; } if (sizeof(X) != 1) return 0; } if (X != 7) return 0; } return X(); }' symbol-nested-kind-shadowing
check 42 'typedef int T; int f(enum { T = 42 } x) { return T + x; } int main(void) { T x = 0; return f(x); }' symbol-parameter-enum-restores-typedef
check 42 'int f(int n) { if (n == 0) return 42; { typedef char T; T x = 1; if (x != 1) return 0; } return f(n - 1); } int main(void) { return f(3); }' symbol-recursive-binding-after-block
check_error symbol-parameter-hides-function-call 'int f(int f) { return f(); }' "1:23: error: called object is not a function"
check_error symbol-file-conflict-hidden-by-parameter-enum 'typedef int f; int f(enum { f = 1 } x);' "1:20: error: function conflicts with typedef 'f'"
check_error symbol-file-enum-conflict-hidden-by-parameter 'enum { f }; int f(int f);' "1:17: error: function conflicts with enumerator 'f'"

# Typedef declarations and ordinary identifier scope.
check 42 'typedef struct S; typedef struct S S; struct S { int x; }; int main(void) { S s; s.x = 42; return s.x; }' typedef-tag-only
check 42 'typedef enum { A = 42 }; int main(void) { return A; }' typedef-enum-only
check 42 'typedef int I; int main(void) { I x = 42; return x; }' typedef-integer
check 42 'int main(void) { typedef int I; I x = 42; return x; }' typedef-local
check 1 'typedef unsigned U; int main(void) { U x = 4294967295U; return x > 0; }' typedef-unsigned
check 255 'typedef char C; int main(void) { C x = 255; return x; }' typedef-char
check 1 'typedef unsigned char C; int main(void) { C x = 255; return x == 255; }' typedef-unsigned-char
check 255 'typedef short S; int main(void) { S x = 65535; return x; }' typedef-short
check 1 'typedef unsigned short S; int main(void) { S x = 65535; return x == 65535; }' typedef-unsigned-short
check 8 'typedef long L; int main(void) { return sizeof(L); }' typedef-long
check 1 'typedef unsigned long long U; int main(void) { U x = 18446744073709551615ULL; return x > 0; }' typedef-unsigned-long-long
check 42 'typedef int I; typedef I J; int main(void) { J x = 42; return x; }' typedef-chain
check 42 'typedef int *P; int main(void) { int x = 42; P p = &x; return *p; }' typedef-pointer
check 42 'typedef int I; int main(void) { I x = 42; I *p = &x; return *p; }' typedef-pointer-declarator
check 42 'typedef int A[3]; int main(void) { A a; a[2] = 42; return a[2]; }' typedef-array
check 12 'typedef int A[3]; int main(void) { return sizeof(A); }' typedef-array-sizeof
check 42 'typedef int A[3]; int f(A a) { return a[2]; } int main(void) { A a; a[2] = 42; return f(a); }' typedef-array-parameter
check 42 'typedef struct { int x; } S; int main(void) { S s; s.x = 42; return s.x; }' typedef-anonymous-struct
check 42 'typedef struct Node Node; struct Node { int value; Node *next; }; int main(void) { Node n; n.value = 42; n.next = &n; return n.next->value; }' typedef-forward-struct
check 42 'typedef enum { ANSWER = 42 } E; int main(void) { E e = ANSWER; return e; }' typedef-enum
check 42 'typedef int I; struct S { I x; }; int main(void) { struct S s; s.x = 42; return s.x; }' typedef-struct-member
check 42 'typedef int I; I f(I x); I f(I x) { return x; } int main(void) { return f(42); }' typedef-function-signature
check 42 'typedef void V; V f(V) { return; } int main(V) { f(); return 42; }' typedef-void
check 42 'typedef int I; typedef I I; int main(void) { I x = 42; return x; }' typedef-repeat
check 8 'typedef int *P; typedef int *P; int main(void) { return sizeof(P); }' typedef-repeat-pointer
check 12 'typedef int A[3]; typedef int A[3]; int main(void) { return sizeof(A); }' typedef-repeat-array
check 4 'typedef enum E { A } E; typedef enum E E; int main(void) { return sizeof(E); }' typedef-repeat-enum
check 42 'int main(void) { int T = 42; { typedef char T; T x = 1; if (sizeof(x) != 1) return 0; } return T; }' typedef-hides-variable
check 42 'enum { T = 42 }; int main(void) { { typedef char T; T x; if (sizeof(x) != 1) return 0; } return T; }' typedef-hides-enumerator
check 42 'int T(void) { return 42; } int main(void) { { typedef char T; T x; if (sizeof(x) != 1) return 0; } return T(); }' typedef-hides-function
check 42 'typedef int T; int main(void) { { int T = 42; return T; } }' typedef-variable-hides-alias
check 42 'typedef int T; int main(void) { enum { T = 42 }; return T; }' typedef-enumerator-hides-alias
check 42 'typedef int T; int f(int T) { return T; } int main(void) { return f(42); }' typedef-parameter-hides-alias
check 42 'typedef int T; int f(int T); int main(void) { T x = 42; return x; }' typedef-prototype-restores-alias
check 42 'typedef int T; int f(int T) { return T; } int main(void) { T x = 42; return f(x); }' typedef-definition-restores-alias
check 4 'typedef int T; int main(void) { { typedef char T; if (sizeof(T) != 1) return 0; } return sizeof(T); }' typedef-block-restores-alias
check 42 'typedef int T; int main(void) { int sum = 0; for (T i = 0; i < 7; i++) sum += 6; return sum; }' typedef-for-type
check 4 'typedef int T; int main(void) { for (int T = 0; T < 1; T++) {} return sizeof(T); }' typedef-for-restores-alias
check 4 'typedef int T; int main(void) { for (int i = 0; i < 1; i++) { typedef char T; if (sizeof(T) != 1) return 0; } return sizeof(T); }' typedef-for-body-restores-alias
check 42 'typedef int T; int main(void) { goto T; T: return 42; }' typedef-label-namespace
check 42 'typedef int T; struct T { int T; }; int main(void) { struct T t; t.T = 42; return t.T; }' typedef-tag-member-namespace
check 42 'typedef int I, *P, A[2]; int main(void) { A a; P p = a; I x = 42; p[1] = x; return a[1]; }' typedef-multiple-declarators
check 42 'typedef int I, A[sizeof(I)]; int main(void) { A a; a[3] = 42; return a[3]; }' typedef-earlier-declarator-visible
check 42 'int typedef I; int main(void) { I x = 42; return x; }' typedef-storage-after-type
check 42 'unsigned typedef long U; int main(void) { U x = 42; return x; }' typedef-storage-between-types
check 42 'typedef int I; I typedef J; int main(void) { J x = 42; return x; }' typedef-storage-after-alias
check 42 'struct S { int x; } typedef S; int main(void) { S s; s.x = 42; return s.x; }' typedef-storage-after-struct
check 42 'typedef int T; int main(void) { int T = 42; return sizeof(T) + T - 4; }' typedef-sizeof-hidden-alias
check 42 'typedef int T; int main(void) { T T = 42; return T; }' typedef-alias-as-variable-name
check 42 'typedef int T; int f(T T) { return T; } int main(void) { return f(42); }' typedef-alias-as-parameter-name
check 42 'typedef int T; int main(void) { T: ; T x = 42; return x; }' typedef-label-preserves-alias

check_error typedef-conflicting-type 'typedef int T; typedef long T;' "1:29: error: conflicting typedef for 'T'"
check_error typedef-conflicting-array 'typedef int A[2]; typedef int A[3];' "1:31: error: conflicting typedef for 'A'"
check_error typedef-enum-integer-distinct 'typedef enum E { A } T; typedef int T;' "1:37: error: conflicting typedef for 'T'"
check_error typedef-enum-pointer-distinct 'typedef enum E { A } *P; typedef int *P;' "1:39: error: conflicting typedef for 'P'"
check_error typedef-variable-conflict 'int main(void) { typedef int T; int T; }' "1:37: error: variable conflicts with typedef 'T'"
check_error typedef-local-conflict 'int main(void) { int T; typedef int T; }' "1:37: error: typedef conflicts with variable 'T'"
check_error typedef-enumerator-conflict 'enum { T }; typedef int T;' "1:25: error: typedef conflicts with enumerator 'T'"
check_error typedef-enum-name-conflict 'typedef int T; enum { T };' "1:23: error: enumerator conflicts with typedef 'T'"
check_error typedef-function-conflict 'int f(void); typedef int f;' "1:26: error: typedef conflicts with function 'f'"
check_error typedef-function-name-conflict 'typedef int f; int f(void);' "1:20: error: function conflicts with typedef 'f'"
check_error typedef-function-conflict-hidden-by-parameter 'typedef int f; int f(int f) { return f; }' "1:20: error: function conflicts with typedef 'f'"
check_error typedef-parameter-conflict 'int f(int x) { typedef int x; return x; }' "1:28: error: typedef conflicts with variable 'x'"
check_error typedef-initializer 'typedef int T = 1;' "1:15: error: typedef cannot have an initializer"
check_error typedef-missing-name 'typedef int;' "1:12: error: expect typedef name"
check_error typedef-missing-pointer-alias 'typedef int *;' "1:14: error: expect typedef name"
check_error typedef-duplicate-storage 'typedef typedef int T;' "1:9: error: duplicate typedef specifier"
check_error typedef-storage-in-sizeof 'int main(void) { return sizeof(typedef int); }' "1:32: error: typedef is not allowed here"
check_error typedef-storage-in-member 'struct S { typedef int T; };' "1:12: error: typedef is not allowed here"
check_error typedef-storage-in-parameter 'int f(typedef int T);' "1:7: error: typedef is not allowed here"
check_error typedef-name-as-expression 'typedef int T; int main(void) { return T; }' "1:40: error: typedef name is not an expression"
check_error typedef-name-as-call 'typedef int T; int main(void) { return T(); }' "1:40: error: typedef name is not an expression"
check_error typedef-storage-in-for 'int main(void) { for (typedef int T; ; ) {} }' "1:23: error: typedef is not allowed in a for initializer"
check_error typedef-declaration-as-body 'typedef int T; int main(void) { if (1) T x; }' "1:40: error: expect statement"
check_error typedef-typedef-as-body 'int main(void) { if (1) typedef int T; }' "1:25: error: expect statement"
check_error typedef-function-scope-restored 'int f(void) { typedef int T; return 0; } int main(void) { T x; return 0; }' "1:59: error: undeclared identifier"
check_error typedef-hidden-parameter-type 'typedef int T; int f(int T, T x);' "1:29: error: expect type specifier"
check_error typedef-function-type-unsupported 'typedef int F(int);' "1:14: error: function typedefs are not supported yet"

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
# Integer suffix spelling and token boundaries.
for suffix in U u L l UL Ul uL ul LU Lu lU lu LL ll ULL Ull uLL ull LLU LLu llU llu; do
    check 42 "int main(void) { return 42$suffix; }" "integer-suffix-decimal-$suffix"
    check 42 "int main(void) { return 052$suffix; }" "integer-suffix-octal-$suffix"
done
check 42 'int main(void) { return 1U+051L; }' integer-suffix-before-operator
check 0 'int main(void) { return 0uL; }' integer-suffix-zero
check_error integer-suffix-duplicate-unsigned 'int main(void) { return 42UU; }' '1:25: error: invalid integer suffix'
check_error integer-suffix-duplicate-unsigned-case 'int main(void) { return 42Uu; }' '1:25: error: invalid integer suffix'
check_error integer-suffix-duplicate-unsigned-after-long 'int main(void) { return 42ULU; }' '1:25: error: invalid integer suffix'
check_error integer-suffix-long-long-mixed-case 'int main(void) { return 42lL; }' '1:25: error: invalid integer suffix'
for suffix in Ll ULl lLU LLL lll lUl LuL ULLU LLUU LL2 ll_name; do
    check_error "integer-suffix-invalid-$suffix" "int main(void) { return 42$suffix; }" '1:25: error: invalid integer suffix'
done
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

# Compound assignments preserve one target address and the operation's type.
for entry in '+= 20 22 42' '-= 50 8 42' '*= 6 7 42' '/= 84 2 42' \
             '%= 85 43 42' '<<= 21 1 42' '>>= 84 1 42' \
             '&= 63 42 42' '^= 43 1 42' '|= 40 2 42'; do
    read -r op initial rhs expected <<< "$entry"
    check 1 "int main(void) { int x = $initial; int *p = &x; return (*p $op $rhs) == $expected && x == $expected; }" "compound-dereference-$op"
    check 1 "int main(void) { int a[2]; a[0] = $initial; a[1] = 99; return (a[0] $op $rhs) == $expected && a[0] == $expected && a[1] == 99; }" "compound-subscript-$op"
    for target in s.x 'p->x'; do
        check 1 "struct S { int x; int guard; }; int main(void) { struct S s; struct S *p = &s; s.x = $initial; s.guard = 99; return ($target $op $rhs) == $expected && s.x == $expected && s.guard == 99; }" "compound-member-$target-$op"
    done
done
for spec in 'char' 'unsigned char' 'short' 'unsigned short' 'int' 'unsigned' 'long' 'unsigned long' 'long long' 'unsigned long long'; do
    check 1 "struct S { char before; $spec x; char after; }; int main(void) { struct S s; $spec *p = &s.x; s.before = 7; s.x = 20; s.after = 9; return (*p += 22) == 42 && s.x == 42 && s.before == 7 && s.after == 9; }" "compound-store-width-$spec"
done
check 1 'struct S { char x; unsigned char y; }; int main(void) { struct S s; s.x = 127; s.y = 255; return (s.x += 1) == -128 && s.x == -128 && (s.y += 1) == 0 && s.y == 0; }' compound-member-char-narrowing
check 1 'int main(void) { short a[2]; a[0] = 32767; a[1] = -32768; return (a[0] += 1) == -32768 && (a[1] -= 1) == 32767 && a[0] == -32768 && a[1] == 32767; }' compound-short-narrowing
check 1 'int main(void) { unsigned short a[1]; a[0] = 65535; return (a[0] += 1) == 0 && (a[0] -= 1) == 65535; }' compound-unsigned-short-narrowing
check 1 'struct S { int x; }; int main(void) { struct S s; s.x = -1; return (s.x /= 2U) == 2147483647 && s.x == 2147483647; }' compound-unsigned-calculation
check 1 'struct S { long x; }; int main(void) { struct S s; s.x = -9; return (s.x /= 2U) == -4 && s.x == -4; }' compound-wide-signed-calculation
check 1 'int main(void) { int a[1]; a[0] = 9; return (a[0] /= 4294967297L) == 0 && a[0] == 0; }' compound-widen-before-division
check 1 'int main(void) { int a[1]; a[0] = -1; return (a[0] /= 2UL) == -1 && a[0] == -1; }' compound-wide-unsigned-before-narrowing
check 1 'int main(void) { char a[1]; a[0] = -9; return (a[0] /= 2U) == -5 && a[0] == -5; }' compound-char-integer-promotion
check 1 'int main(void) { short a[1]; a[0] = -9; return (a[0] /= 2UL) == -5 && a[0] == -5; }' compound-short-wide-conversion
check 1 'int main(void) { unsigned short a[1]; a[0] = 65535; return (a[0] /= -1) == 1 && a[0] == 1; }' compound-unsigned-short-promotion
check 1 'int main(void) { unsigned a[1]; a[0] = 2147483648U; return (a[0] <<= 1ULL) == 0 && a[0] == 0; }' compound-shift-left-type
check 1 'int main(void) { long a[1]; a[0] = -8; return (a[0] >>= 1ULL) == -4 && a[0] == -4; }' compound-shift-independent-promotions
check 1 'struct S { long long x; unsigned long long y; }; int main(void) { struct S s; s.x = 1LL << 40; s.y = 1ULL << 63; return (s.x *= 3) == 3LL << 40 && (s.y >>= 1) == 1ULL << 62; }' compound-wide-members
check 1 'int main(void) { unsigned long long a[1]; a[0] = 18446744073709551615ULL; return (a[0] /= 2) == 9223372036854775807ULL && (a[0] %= 7) == 0; }' compound-wide-unsigned-divmod
check 1 'int main(void) { int a[2]; a[0] = 20; a[1] = 99; int i = 0; int result = (a[i++] += 22); return result == 42 && i == 1 && a[0] == 42 && a[1] == 99; }' compound-index-evaluated-once
check 1 'int main(void) { int a[2]; a[0] = 20; a[1] = 99; int *p = a; int result = (*p++ += 22); return result == 42 && p == a + 1 && a[0] == 42 && a[1] == 99; }' compound-pointer-base-evaluated-once
check 1 'struct S { int x; }; struct S *get(struct S *p, int *n) { (*n)++; return p; } int main(void) { struct S s; s.x = 14; int n = 0; int result = (get(&s, &n)->x *= 3); return result == 42 && n == 1 && s.x == 42; }' compound-call-base-evaluated-once
check 1 'int *get(int *p, int *n) { (*n)++; return p; } int value(int *n) { (*n)++; return 22; } int main(void) { int x = 20; int n = 0; int m = 0; int result = (*get(&x, &n) += value(&m)); return result == 42 && x == 42 && n == 1 && m == 1; }' compound-both-operands-call
check 1 'struct S { int x; }; int main(void) { struct S s; int a[1]; s.x = 0; a[0] = 7; return (s.x += (a[0] *= 6)) == 42 && s.x == 42 && a[0] == 42; }' compound-nested-assignment
check 1 'int main(void) { int a[1]; a[0] = 20; int *p = &(a[0]); return 10 + (*p += 22) == 52 && a[0] == 42; }' compound-result-in-binary-expression
check 1 'struct S { int *p; }; int main(void) { struct S s; int a[3]; s.p = a; return (s.p += 2) == a + 2 && s.p == a + 2 && (s.p -= 1) == a + 1; }' compound-pointer-member-scaling
check 42 'int main(void) { int a[3]; a[1] = 42; int *p[1]; p[0] = a; p[0] += 2; return *(p[0] -= 1); }' compound-pointer-array-element
check 42 'int main(void) { int a[3]; a[2] = 42; int *p = a; int **pp = &p; *pp += 2; return **pp; }' compound-pointer-through-dereference
check 1 'struct S { char c; long x; }; int main(void) { struct S a[3]; struct S *p = a; struct S **pp = &p; return (*pp += 2) == a + 2 && (*pp -= 1) == a + 1; }' compound-struct-pointer-scaling
check 42 'int main(void) { int a[3]; a[1] = 42; int *p = a + 2; int **pp = &p; char offset = -1; *pp += offset; return *p; }' compound-negative-pointer-offset
check 1 'struct S { int a[2]; }; int main(void) { struct S s; s.a[0] = 20; s.a[1] = 99; int i = 0; return (s.a[i++] += 22) == 42 && i == 1 && s.a[1] == 99; }' compound-array-member-index-once
check 42 'struct S { int x; }; struct O { struct S s[2]; }; int main(void) { struct O o; o.s[1].x = 14; o.s[1].x *= 3; return o.s[1].x; }' compound-nested-array-member
check 1 'int rhs(int a, int b, int c, int d, int e, int f, int g) { return g; } int *get(int a, int b, int c, int d, int e, int f, int *p) { return p; } int main(void) { int x = 20; return 10 + (*get(0,0,0,0,0,0,&x) += rhs(0,0,0,0,0,0,22)) == 52 && x == 42; }' compound-stack-calls-and-outer-push
check 1 'int take(int a, int b, int c, int d, int e, int f, int g) { return a + g; } int main(void) { int a[2]; a[0] = 20; a[1] = 14; return take(a[0] += 22,0,0,0,0,0,a[1] *= 3) == 84 && a[0] == 42 && a[1] == 42; }' compound-call-arguments

check_error compound-pointer-plus-pointer 'int main(void) { int a[2]; int *p = a; int *q = a; p += q; return 0; }' '1:54: error: invalid operands to binary +'
check_error compound-pointer-minus-pointer 'int main(void) { int a[2]; int *p = a; int *q = a; p -= q; return 0; }' '1:54: error: incompatible assignment type'
check_error compound-integer-plus-pointer 'int main(void) { int a[1]; int x = 0; x += a; return 0; }' '1:41: error: incompatible assignment type'
check_error compound-pointer-multiply 'int main(void) { int a[1]; int *p = a; p *= 2; return 0; }' '1:42: error: invalid operands to integer operator'
check_error compound-pointer-shift 'int main(void) { int a[1]; int *p = a; p <<= 1; return 0; }' '1:42: error: invalid operands to integer operator'
check_error compound-pointer-member-void 'struct S { void *p; }; int main(void) { struct S s; s.p += 1; return 0; }' '1:57: error: pointer arithmetic on void pointer'
check_error compound-pointer-member-incomplete 'struct S { struct T *p; }; int main(void) { struct S s; s.p += 1; return 0; }' '1:61: error: pointer arithmetic on incomplete type'
check_error compound-array-member-target 'struct S { int a[2]; }; int main(void) { struct S s; s.a += 1; return 0; }' '1:58: error: invalid assignment target'
check_error compound-struct-target 'struct S { int x; }; int main(void) { struct S s; s += 1; return 0; }' '1:51: error: struct values are not supported yet'
check_error compound-result-is-not-lvalue 'int main(void) { int a = 0; (a += 1) += 1; return 0; }' '1:38: error: invalid assignment target'
check_error compound-result-address 'int main(void) { int a = 0; int *p = &(a += 1); return 0; }' '1:38: error: invalid address-of target'
check_error compound-result-not-null-constant 'int main(void) { int a = 0; int *p = (a += 0); return 0; }' '1:34: error: incompatible initializer type'
check_error compound-member-void-rhs 'void f(void); struct S { int x; }; int main(void) { struct S s; s.x += f(); return 0; }' '1:72: error: void expression used as a value'

# sizeof keeps array types and does not evaluate expression operands.
for entry in 'char:1' 'unsigned char:1' 'short:2' 'short int:2' \
             'unsigned short int:2' 'int:4' 'unsigned:4' 'int unsigned:4' \
             'long:8' 'unsigned long int:8' 'long long:8' \
             'unsigned long long int:8'; do
    spec=${entry%:*}
    size=${entry##*:}
    check "$size" "int main(void) { return sizeof($spec); }" "sizeof-type-$spec"
    check "$size" "int main(void) { $spec x; return sizeof x; }" "sizeof-variable-$spec"
done
check 8 'int main(void) { return sizeof(void *); }' sizeof-void-pointer
check 8 'int main(void) { return sizeof(int **); }' sizeof-pointer-to-pointer
check 8 'struct S; int main(void) { return sizeof(struct S *); }' sizeof-incomplete-pointer
check 12 'int main(void) { return sizeof(int [3]); }' sizeof-array-type
check 24 'int main(void) { return sizeof(char *[3]); }' sizeof-array-of-pointers-type
check 1 'int main(void) { return sizeof(char [2147483647]) == 2147483647UL; }' sizeof-large-array-type
check 12 'int main(void) { int a[3]; return sizeof a; }' sizeof-array-expression
check 12 'int main(void) { int a[3]; return sizeof((a)); }' sizeof-parenthesized-array
check 12 'int main(void) { int a[3]; return sizeof *(&a); }' sizeof-dereferenced-array-address
check 8 'int main(void) { int a[3]; return sizeof(a + 0); }' sizeof-decayed-array-expression
check 8 'int main(void) { int a[3]; return sizeof(0, a); }' sizeof-comma-array-conversion
check 4 'int main(void) { return sizeof "abc"; }' sizeof-string-literal
check 4 'int main(void) { return sizeof("abc"); }' sizeof-parenthesized-string
check 4 'int main(void) { return sizeof "a" "bc"; }' sizeof-concatenated-string
check 4 'int main(void) { return sizeof "a\0b"; }' sizeof-string-embedded-zero
check 1 'int main(void) { return sizeof ""; }' sizeof-empty-string
check 4 "int main(void) { return sizeof 'a'; }" sizeof-character-constant
check 16 'struct S { char c; long n; }; int main(void) { return sizeof(struct S); }' sizeof-struct-type
check 16 'struct S { char c; long n; }; int main(void) { struct S s; return sizeof s; }' sizeof-struct-expression
check 16 'int main(void) { return sizeof(struct { char c; long n; }); }' sizeof-anonymous-struct
check 16 'int main(void) { return sizeof(struct S { char c; long n; }); }' sizeof-inline-tag-definition
check 1 'int main(void) { sizeof(struct S { long x; }); struct S s; return sizeof s == 8; }' sizeof-type-tag-visibility
check 1 'struct I { char c; long n; }; struct S { char c; struct I inner; int a[3]; }; int main(void) { struct S s; return sizeof s == 40 && sizeof s.inner == 16 && sizeof s.a == 12 && sizeof s.a[0] == 4; }' sizeof-struct-members
check 1 'struct S { char c; int a[3]; }; int main(void) { struct S *p = 0; return sizeof p->c == 1 && sizeof p->a == 12 && sizeof *p == 16; }' sizeof-null-pointer-members
check 8 'int f(int a[3]) { return sizeof a; } int main(void) { int a[3]; return f(a); }' sizeof-array-parameter-is-pointer
check 1 'int main(void) { char c; return sizeof c == 1 && sizeof(c + 0) == 4 && sizeof ~c == 4 && sizeof !c == 4; }' sizeof-expression-promotions
check 1 'int main(void) { int i = 0; return sizeof(i++) == 4 && i == 0; }' sizeof-postincrement-unevaluated
check 1 'int main(void) { char c = 0; return sizeof ++c == 1 && c == 0; }' sizeof-preincrement-unevaluated
check 1 'int main(void) { int i = 0; return sizeof(i = 42) == 4 && i == 0; }' sizeof-assignment-unevaluated
check 1 'int main(void) { short a[2]; int i = 0; return sizeof(a[i++] += 42) == 2 && i == 0; }' sizeof-compound-assignment-unevaluated
check 1 'int main(void) { int a[2]; int i = 0; return sizeof a[i++] == 4 && i == 0; }' sizeof-subscript-unevaluated
check 1 'int main(void) { int a[2]; int *p = a; return sizeof *p++ == 4 && p == a; }' sizeof-pointer-increment-unevaluated
check 1 'int main(void) { int *p = 0; return sizeof *p == 4; }' sizeof-null-dereference-unevaluated
check 4 'int main(void) { return sizeof(1 / 0); }' sizeof-division-not-evaluated
check 8 'int main(void) { return sizeof(9223372036854775807L + 1L); }' sizeof-overflow-not-evaluated
check 1 'int f(int *p) { (*p)++; return 42; } int main(void) { int n = 0; return sizeof f(&n) == 4 && n == 0; }' sizeof-call-unevaluated
check 1 'char external(void); int main(void) { return sizeof external(); }' sizeof-call-no-link-reference
check 4 'struct S { int x; }; struct S *external(void); int main(void) { return sizeof external()->x; }' sizeof-member-call-unevaluated
check 8 'int main(void) { return sizeof sizeof(char); }' sizeof-result-width
check 1 'int main(void) { return sizeof(int) > 0 && sizeof(int) < -1; }' sizeof-result-unsigned
check 6 'int main(void) { return sizeof(int) + 2; }' sizeof-type-precedence
check 8 'int main(void) { return sizeof 1 + sizeof 2; }' sizeof-unary-precedence
check 1 'int main(void) { int a[sizeof(int)]; a[3] = 42; return sizeof a == 16 && a[3] == 42; }' sizeof-array-bound
check 42 'int main(void) { switch (4) { case sizeof(int): return 42; } return 0; }' sizeof-case-constant
check 1 'int main(void) { int a[2]; return sizeof(sizeof a) == 8 && sizeof(a[0]++) == 4; }' sizeof-nested-expressions

check_error sizeof-void 'int main(void) { return sizeof(void); }' '1:25: error: sizeof cannot be applied to void'
check_error sizeof-void-expression 'void f(void); int main(void) { return sizeof f(); }' '1:39: error: sizeof cannot be applied to void'
check_error sizeof-incomplete-struct 'struct S; int main(void) { return sizeof(struct S); }' '1:35: error: sizeof cannot be applied to an incomplete type'
check_error sizeof-incomplete-dereference 'int main(void) { struct S *p; return sizeof *p; }' '1:38: error: sizeof cannot be applied to an incomplete type'
check_error sizeof-named-type 'int main(void) { return sizeof(int x); }' '1:36: error: type name cannot contain an identifier'
check_error sizeof-named-pointer-type 'int main(void) { return sizeof(int *x); }' '1:37: error: type name cannot contain an identifier'
check_error sizeof-named-array-type 'int main(void) { return sizeof(int x[2]); }' '1:36: error: type name cannot contain an identifier'
check_error sizeof-empty-operand 'int main(void) { return sizeof(); }' '1:32: error: expect expr'
check_error sizeof-missing-operand 'int main(void) { return sizeof; }' '1:31: error: expect expr'
check_error sizeof-type-without-parens 'int main(void) { return sizeof int; }' '1:32: error: expect expr'
check_error sizeof-missing-rparen 'int main(void) { return sizeof(int; }' '1:35: error: expect '\'')'\'''
check_error sizeof-missing-array-rbracket 'int main(void) { return sizeof(int[2); }' '1:37: error: expect '\'']'\'''
check_error sizeof-incomplete-array 'int main(void) { return sizeof(int[]); }' '1:36: error: array bound is required'
check_error sizeof-void-array 'int main(void) { return sizeof(void[2]); }' '1:36: error: array element has void type'
check_error sizeof-incomplete-struct-array 'int main(void) { return sizeof(struct S[2]); }' '1:40: error: array element has incomplete type'
check_error sizeof-zero-array-bound 'int main(void) { return sizeof(int[0]); }' '1:36: error: array bound must be positive'
check_error sizeof-variable-array-bound 'int main(void) { int n = 3; return sizeof(int[n]); }' '1:48: error: not an integer constant expression'
check_error sizeof-undeclared-operand 'int main(void) { return sizeof missing; }' '1:32: error: undeclared identifier'
check_error sizeof-invalid-assignment-operand 'int main(void) { return sizeof(1 = 2); }' '1:34: error: invalid assignment target'
check_error sizeof-missing-member 'struct S { int x; }; int main(void) { struct S s; return sizeof s.y; }' '1:67: error: no member named '\''y'\'' in struct'
check_error sizeof-invalid-call 'int f(int x); int main(void) { return sizeof f(); }' '1:46: error: wrong number of arguments to function '\''f'\'''
check_error sizeof-multiline-void $'int main(void) {\n  return sizeof(void);\n}\n' '2:10: error: sizeof cannot be applied to void'
check_error sizeof-multiline-type-name $'int main(void) {\n  return sizeof(int name);\n}\n' '2:21: error: type name cannot contain an identifier'

# Enum constants are int; enum objects use our signed int representation.
check 6 'enum Color { RED, GREEN = 5, BLUE }; int main(void) { enum Color color = BLUE; return color; }' enum-basic
check 1 'enum { A, B, C }; int main(void) { return A == 0 && B == 1 && C == 2; }' enum-default-values
check 1 'enum { A = 5, B = 5, C }; int main(void) { return A == B && C == 6; }' enum-duplicate-values
check 1 'enum E { A = -2, B, C, D }; int main(void) { return A == -2 && B == -1 && C == 0 && D == 1; }' enum-negative-values
check 42 'int main(void) { enum { ANSWER = 42, }; return ANSWER; }' enum-local-anonymous-trailing-comma
check 42 'enum E { A = 1, B = (A << 5) | 10 }; int main(void) { return B; }' enum-constant-expression
check 42 'enum { A = 2, B = A * 21, C = A ? B : 0 }; int main(void) { return C; }' enum-reference-and-conditional
check 4 'enum { SIZE = sizeof(int) }; int main(void) { return SIZE; }' enum-sizeof-initializer
check 1 'enum { A = 2147483647, B = -2147483648, C = 0 }; int main(void) { return A == 2147483647 && B == -2147483648 && C == 0; }' enum-int-boundaries
check 1 'enum { A = 2147483647, }; int main(void) { return A == 2147483647; }' enum-max-final-value
check 1 'enum { A = 0 }; int main(void) { return sizeof A == 4 && A - 1 < 0; }' enum-constant-has-int-type
check 1 'enum { ZERO }; int main(void) { int *p = ZERO; return p == 0; }' enum-zero-null-pointer-constant
check 42 'enum { COUNT = 3 }; int main(void) { int a[COUNT]; a[COUNT - 1] = 42; return a[2]; }' enum-array-bound
check 42 'enum E { A = 1, B = 42 }; int main(void) { enum E e = A; switch (e) { case A: return B; default: return 0; } }' enum-switch
check 1 'enum E { A = -1, B = 0, C = 3 }; int main(void) { int sum = 0; for (enum E e = B; e <= C; e++) sum += e; return sum == 6; }' enum-for-variable
check 42 'enum E { A = -1, B = 42 }; enum E id(enum E x) { return x; } int main(void) { return id(B); }' enum-function-signature
check 1 'enum E { A = -1 }; enum E f(void) { return A; } int main(void) { long x = f(); return x == -1 && f() < 0; }' enum-return-sign-extension
check 1 'enum E { A = -1 }; enum E f(int a, int b, int c, int d, int e, int f, enum E g) { return g; } int main(void) { return f(0,0,0,0,0,0,A) == -1; }' enum-stack-parameter
check 1 'enum E { A = -1, B = 42 }; int main(void) { enum E a[2]; a[0] = A; a[1] = B; enum E *p = a; return p[0] == -1 && p[1] == 42 && sizeof a == 8; }' enum-array-storage
check 1 'enum E { A = -1, B = 42 }; struct S { char c; enum E e; char end; }; int main(void) { struct S s; s.c = 7; s.e = B; s.end = 9; return s.e == 42 && s.c == 7 && s.end == 9 && sizeof s == 12; }' enum-struct-member
check 1 'enum E { A = -1, B = 42 }; int main(void) { enum E e = A; return (e += 43) == B && e-- == B && e == 41 && ++e == B; }' enum-compound-and-increment
check 1 'enum E { A = -1 }; int main(void) { enum E e = 4294967295UL; return e == -1 && (e = 4294967295UL) == -1; }' enum-integer-conversion
check 42 'enum E { A = -1, B = 42 }; int main(void) { enum E e = B; int *p = &e; return *p; }' enum-compatible-int-pointer
check 42 'enum E { A = -1, B = 42 }; int f(enum E); int f(int x) { return x; } int main(void) { return f(B); }' enum-compatible-int-prototype
check 42 'enum E { A = -1, B = 42 }; int f(); int f(enum E x) { return x; } int main(void) { enum E e = B; return f(e); }' enum-default-argument-promotion
check 1 'enum E { A = -1 }; int f(); int main(void) { enum E e = A; return f(e); } int f(int x) { return x == -1; }' enum-unprototyped-call-promotion
check 42 'enum E { A = 42 }; enum E; int main(void) { enum E e = A; return e; }' enum-tag-redeclaration
check 42 'enum E { E = 42 }; int main(void) { enum E e = E; return e; }' enum-tag-and-enumerator-namespace
check 42 'enum { A = 42 }; int main(void) { int A = 7; { enum { A = 42 }; if (A != 42) return 0; } return A == 7 ? 42 : 0; }' enum-shadow-variable
check 42 'enum { A = 7 }; int main(void) { { enum { A = A + 35 }; if (A != 42) return 0; } return A == 7 ? 42 : 0; }' enum-initializer-sees-outer-constant
check 42 'enum { A = 7 }; int main(void) { int A = 42; return A; }' enum-variable-shadows-constant
check 42 'enum { A = 7 }; int f(int A) { return A; } int main(void) { return f(42); }' enum-parameter-shadows-global-constant
check 42 'enum E { A = 7 }; int main(void) { { enum E { A = 42 }; enum E e = A; if (e != 42) return 0; } enum E e = A; return e == 7 ? 42 : 0; }' enum-tag-and-constant-shadowing
check 42 'int main(void) { { enum E { A }; } { enum E { A = 42 }; return A; } }' enum-sibling-scopes
check 42 'int first(void) { enum { A = 1 }; return A; } int main(void) { enum { A = 42 }; return A; }' enum-function-scope-restore
check 42 'int main(void) { for (enum { A = 1 } e = A; e < 2; e++) {} enum { A = 42 }; return A; }' enum-for-scope-restore
check 42 'int f(enum E { A = 42 } e) { enum E x = e; return x + A - 42; } int main(void) { enum E { A = 1 } e = A; return f(42); }' enum-parameter-definition-scope
check 42 'int f(enum E { A = 1 } e); int main(void) { enum E { A = 42 } e = A; return e; }' enum-prototype-scope-restore
check 42 'struct S { enum E { A = 42 } e; }; int main(void) { struct S s; enum E e = A; s.e = e; return s.e; }' enum-definition-in-struct
check 42 'int main(void) { enum E { A = 42 } e = A; { struct E { int x; }; struct E s; s.x = e; return s.x; } }' enum-ordinary-and-member-names
check 42 'enum { A = 42 }; struct S { int A; }; int main(void) { struct S s; s.A = A; return s.A; }' enum-member-namespace
check 42 'enum { A = 42 }; int main(void) { goto A; return 0; A: return A; }' enum-label-namespace
check 42 'int main(void) { return sizeof(enum E { A = 42 }) == 4 ? A : 0; }' enum-definition-in-type-name
check 1 'enum E { A = -1, B = 42 }; int main(void) { return sizeof(enum E) == 4 && sizeof(A) == sizeof(int); }' enum-sizeof
check 1 'enum { A = 0 && (1 / 0), B = 1 ? 42 : (1 / 0) }; int main(void) { return A == 0 && B == 42; }' enum-short-circuit-constant-expression

check_error enum-empty 'enum E {};' '1:9: error: expect enumerator name'
check_error enum-anonymous-empty 'enum {};' '1:7: error: expect enumerator name'
check_error enum-missing-tag 'enum;' '1:5: error: expect enum tag or '\''{'\'''
check_error enum-missing-enumerator 'enum E { A, , B };' '1:13: error: expect enumerator name'
check_error enum-keyword-enumerator 'enum E { int };' '1:10: error: expect enumerator name'
check_error enum-missing-initializer 'enum E { A = };' '1:14: error: expect expr'
check_error enum-missing-comma 'enum E { A B };' '1:12: error: expect '\''}'\'''
check_error enum-missing-rbrace 'enum E { A;' '1:11: error: expect '\''}'\'''
check_error enum-eof 'enum E { A' '1:11: error: expect '\''}'\'''
check_error enum-duplicate-name 'enum E { A, A };' '1:13: error: duplicate enumerator '\''A'\'''
check_error enum-duplicate-across-definitions 'enum E { A }; enum F { A };' '1:24: error: duplicate enumerator '\''A'\'''
check_error enum-redefinition 'enum E { A }; enum E { B };' '1:20: error: redefinition of enum '\''E'\'''
check_error enum-undefined-tag 'int main(void) { enum E e; return 0; }' '1:23: error: enum '\''E'\'' is not defined'
check_error enum-forward-declaration 'enum E;' '1:6: error: enum '\''E'\'' is not defined'
check_error enum-self-type-incomplete 'enum E { A = sizeof(enum E) };' '1:26: error: enum '\''E'\'' is not defined'
check_error enum-struct-tag-conflict 'struct E { int x; }; enum E { A };' '1:27: error: tag '\''E'\'' declared as a different kind'
check_error enum-tag-struct-conflict 'enum E { A }; struct E { int x; };' '1:22: error: tag '\''E'\'' declared as a different kind'
check_error enum-struct-reference-conflict 'enum E { A }; int main(void) { struct E *p; return 0; }' '1:39: error: tag '\''E'\'' declared as a different kind'
check_error enum-reference-struct-conflict 'struct E { int x; }; int main(void) { enum E e; return 0; }' '1:44: error: tag '\''E'\'' declared as a different kind'
check_error enum-outside-block 'int main(void) { { enum { A }; } return A; }' '1:41: error: undeclared identifier'
check_error enum-outside-function 'int f(void) { enum { A }; return A; } int main(void) { return A; }' '1:63: error: undeclared identifier'
check_error enum-outside-for 'int main(void) { for (enum { A } e = A; e < 1; e++) {} return A; }' '1:63: error: undeclared identifier'
check_error enum-tag-outside-block 'int main(void) { { enum E { A }; } enum E e; return 0; }' '1:41: error: enum '\''E'\'' is not defined'
check_error enum-param-tag-outside-prototype 'int f(enum E { A } e); int main(void) { enum E e; return 0; }' '1:46: error: enum '\''E'\'' is not defined'
check_error enum-param-constant-outside-prototype 'int f(enum E { A } e); int main(void) { return A; }' '1:48: error: undeclared identifier'
check_error enum-variable-name-conflict 'int main(void) { int A; enum { A }; return 0; }' '1:32: error: enumerator conflicts with variable '\''A'\'''
check_error enum-name-variable-conflict 'int main(void) { enum { A }; int A; return 0; }' '1:34: error: variable conflicts with enumerator '\''A'\'''
check_error enum-parameter-name-conflict 'int f(int A) { enum { A }; return 0; }' '1:23: error: enumerator conflicts with variable '\''A'\'''
check_error enum-name-parameter-conflict 'int f(enum { A } A);' '1:18: error: parameter conflicts with enumerator '\''A'\'''
check_error enum-previous-parameter-conflict 'int f(int A, enum { A } x);' '1:21: error: enumerator conflicts with variable '\''A'\'''
check_error enum-function-name-conflict 'int f(void); enum { f };' '1:21: error: enumerator conflicts with function '\''f'\'''
check_error enum-name-function-conflict 'enum { f }; int f(void);' '1:17: error: function conflicts with enumerator '\''f'\'''
check_error enum-constant-call 'enum { A }; int main(void) { return A(); }' '1:37: error: called object is not a function'
check_error enum-shadow-function-call 'int f(void); int main(void) { enum { f }; return f(); }' '1:50: error: called object is not a function'
check_error enum-constant-assignment 'enum { A }; int main(void) { A = 1; return 0; }' '1:32: error: invalid assignment target'
check_error enum-constant-increment 'enum { A }; int main(void) { A++; return 0; }' '1:31: error: invalid increment target'
check_error enum-constant-address 'enum { A }; int main(void) { int *p = &A; return 0; }' '1:39: error: invalid address-of target'
check_error enum-too-large 'enum { A = 2147483648L };' '1:8: error: enumerator value is outside the range of int'
check_error enum-too-small 'enum { A = -2147483649L };' '1:8: error: enumerator value is outside the range of int'
check_error enum-unsigned-too-large 'enum { A = 4294967295U };' '1:8: error: enumerator value is outside the range of int'
check_error enum-unsigned-high-bit 'enum { A = 18446744073709551615ULL };' '1:8: error: enumerator value is outside the range of int'
check_error enum-implicit-overflow 'enum { A = 2147483647, B };' '1:24: error: enumerator value is outside the range of int'
check_error enum-noninteger-initializer 'enum { A = "text" };' '1:12: error: enumerator value must have integer type'
check_error enum-nonconstant-initializer 'int main(void) { int x = 1; enum { A = x }; return 0; }' '1:42: error: not an integer constant expression'
check_error enum-self-reference 'enum { A = A };' '1:12: error: undeclared identifier'
check_error enum-division-by-zero 'enum { A = 1 / 0 };' '1:18: error: division by zero in constant expression'
check_error enum-incompatible-pointers 'enum E { A = -1 }; enum F { B = -2 }; int main(void) { enum E *e; enum F *f = e; return 0; }' '1:75: error: incompatible initializer type'
check_error enum-incompatible-prototypes 'enum E { A = -1 }; enum F { B = -2 }; int f(enum E); int f(enum F);' '1:58: error: conflicting declaration of function '\''f'\'''
check_error enum-syntax-location $'enum E {\n  A,\n  ,\n};\n' '3:3: error: expect enumerator name'
check_error enum-duplicate-location $'enum E {\n  A,\n  A\n};\n' '3:3: error: duplicate enumerator '\''A'\'''
check_error enum-range-location $'enum E {\n  A = 2147483648L\n};\n' '2:3: error: enumerator value is outside the range of int'

# Parameters and outermost body declarations share depth 1; nested blocks shadow.
check 42 'int f(int x) { { int x = 7; if (x != 7) return 0; } return x; } int main(void) { return f(42); }' parameter-nested-variable-shadow
check 42 'int f(int x) { { enum { x = 7 }; if (x != 7) return 0; } return x; } int main(void) { return f(42); }' parameter-nested-enumerator-shadow
check 42 'int f(int x) { for (int x = 0; x < 2; x++) {} return x; } int main(void) { return f(42); }' parameter-for-variable-shadow
check 42 'int f(enum { A = 7 } x) { { int A = 42; return A; } } int main(void) { return f(0); }' parameter-enumerator-nested-variable-shadow
check 42 'int f(int x); enum { x = 42 }; int main(void) { return x; }' parameter-prototype-scope-restored
check 42 'int f(int x) { int y = 0; return x + y; } enum { x = 40, y = 2 }; int main(void) { return x + y; }' parameter-definition-scope-restored

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
check_error typed-missing-pointer-return 'int *f(void) { return; }' '1:16: error: expect return expr'

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

# String literal lexer diagnostics.
check_error string-unterminated 'int main(void) { return "hello; }' '1:25: error: unterminated string literal'
check_error string-missing-closing-quote 'int main(void) { return "; }' '1:25: error: unterminated string literal'
check_error string-trailing-backslash 'int main(void) { return "hello\; }' '1:25: error: unterminated string literal'
check_error string-raw-newline $'int main(void) { return "hello\nworld"; }' '1:25: error: unterminated string literal'
check_error string-raw-crlf $'int main(void) { return "hello\r\nworld"; }' '1:25: error: unterminated string literal'
check_error string-escaped-newline $'int main(void) { return "hello\\\nworld"; }' '1:25: error: unterminated string literal'
check_error string-invalid-escape 'int main(void) { return "\q"; }' '1:25: error: invalid escape sequence in string literal'
check_error string-invalid-octal-escape 'int main(void) { return "\8"; }' '1:25: error: invalid escape sequence in string literal'
check_error string-empty-hex-escape 'int main(void) { return "\x"; }' '1:25: error: expect hexadecimal digit in character escape'
check_error string-invalid-hex-escape 'int main(void) { return "\xg"; }' '1:25: error: expect hexadecimal digit in character escape'
check_error string-octal-escape-overflow 'int main(void) { return "\400"; }' '1:25: error: character escape out of range'
check_error string-hex-escape-overflow 'int main(void) { return "\x100"; }' '1:25: error: character escape out of range'
check_error string-long-hex-escape-overflow 'int main(void) { return "\xffffffffffffffffffff"; }' '1:25: error: character escape out of range'

# String literal types and invalid expression contexts.
check_error string-integer-initializer 'int main(void) { int x = "abc"; return 0; }' '1:22: error: incompatible initializer type'
check_error string-pointer-initializer-type 'int main(void) { int *p = "abc"; return 0; }' '1:23: error: incompatible initializer type'
check_error string-integer-argument 'int f(int); int main(void) { return f("abc"); }' '1:39: error: incompatible argument type'
check_error string-assignment-target 'int main(void) { "a" = "b"; return 0; }' '1:22: error: invalid assignment target'
check_error string-increment-target 'int main(void) { ++"abc"; return 0; }' '1:18: error: invalid increment target'
check_error string-address-pointer-type 'int main(void) { char *p = &"abc"; return 0; }' '1:24: error: incompatible initializer type'
check_error string-invalid-addition 'int main(void) { "a" + "b"; return 0; }' '1:22: error: invalid operands to binary +'
check_error string-switch-condition 'int main(void) { switch ("abc") { default: return 0; } }' '1:26: error: switch condition must have integer type'
check_error string-case-label 'int main(void) { switch (0) { case "a": return 0; } return 1; }' '1:36: error: case label must have integer type'
check_error string-array-bound 'int main(void) { char a["abc"]; return 0; }' '1:25: error: array bound must have integer type'
check_error string-parenthesized-concat 'int main(void) { ("a") "b"; return 0; }' '1:23: error: expect '\'';'\'''

# Fixed-size array declarations and array parameter adjustment.
for spec in 'char' 'unsigned char' 'short' 'unsigned short' 'int' 'unsigned' \
    'long' 'unsigned long' 'long long' 'unsigned long long'; do
    check 42 "int main(void) { $spec a[3]; return 42; }" "array-declaration-$spec"
done
check 42 'int main(void) { int *a[3]; void *b[2]; int **c[1]; return 42; }' array-pointer-elements
check 42 'int main(void) { int a[1 + 2 * 3]; char b[1 ? 5 : 2]; short c[1U << 3]; return 42; }' array-constant-bounds
check 42 'int main(void) { int a[3]; { char a[5]; } int x = 42; return x; }' array-scope-and-following-local
check 42 'int main(void) { for (int a[3]; 0;) ; return 42; }' array-for-declaration
check 42 'int f(int a[1]) { return *a; } int main(void) { int x = 42; return f(&x); }' array-parameter-definition
check 42 'int f(int [3]); int f(int *p) { return *p; } int main(void) { int x = 42; return f(&x); }' array-unnamed-parameter
check 42 'int f(int a[2]); int f(int a[7]); int f(int *a) { return *a; } int main(void) { int x = 42; return f(&x); }' array-parameter-compatible-bounds
check 42 'int *f(int *a[1]) { return *a; } int main(void) { int x = 42; int *p = &x; return *f(&p); }' array-pointer-parameter
check 42 'int f(char a[1]) { *a = 42; return *a; } int main(void) { char x; return f(&x); }' array-char-parameter

check_error array-zero-bound 'int main(void) { int a[0]; return 0; }' '1:24: error: array bound must be positive'
check_error array-negative-bound 'int main(void) { int a[-1]; return 0; }' '1:24: error: array bound must be positive'
check_error array-missing-bound 'int main(void) { int a[]; return 0; }' '1:24: error: array bound is required'
check_error array-variable-bound 'int main(void) { int n = 3; int a[n]; return 0; }' '1:36: error: not an integer constant expression'
check_error array-call-bound 'int f(void); int main(void) { int a[f()]; return 0; }' '1:40: error: not an integer constant expression'
check_error array-pointer-bound 'int main(void) { int n; int a[&n]; return 0; }' '1:31: error: array bound must have integer type'
check_error array-void-element 'int main(void) { void a[3]; return 0; }' '1:24: error: array element has void type'
check_error array-missing-rbracket 'int main(void) { int a[3; return 0; }' '1:25: error: expect '\'']'\'''
check_error array-large-bound 'int main(void) { int a[536870912]; return 0; }' '1:24: error: array size exceeds supported limit'
check_error array-unsigned-large-bound 'int main(void) { char a[18446744073709551615ULL]; return 0; }' '1:25: error: array size exceeds supported limit'
check_error array-stack-overflow 'int main(void) { char a[2147483647]; return 0; }' '1:23: error: local variables exceed supported stack size'
check_error array-total-stack-overflow 'int main(void) { char a[2147483620]; int b[10]; return 0; }' '1:42: error: local variables exceed supported stack size'
check_error array-multidimensional 'int main(void) { int a[2][3]; return 0; }' '1:26: error: multidimensional arrays are not supported yet'
check_error array-initializer 'int main(void) { int a[2] = {1, 2}; return 0; }' '1:27: error: array initializers are not supported yet'
check_error array-function-return 'int f[3](void);' '1:5: error: function cannot return an array'
check_error array-parameter-conflict 'int f(int a[3]); int f(char *a);' '1:22: error: conflicting declaration of function '\''f'\'''

# Struct declarations, layout, and the tag namespace.
check 42 'struct S { int x; }; int main(void) { struct S s; return 42; }' struct-local-declaration
check 42 'struct S; struct S { int x; }; struct S; int main(void) { return 42; }' struct-forward-definition
check 42 'struct Node { int value; struct Node *next; }; int main(void) { struct Node n; struct Node *p = &n; return p == &n ? 42 : 0; }' struct-self-pointer
check 42 'struct A; struct B { struct A *a; }; struct A { struct B *b; }; int main(void) { struct A a; struct B b; return 42; }' struct-mutual-pointers
check 42 'int main(void) { struct { int x; char y; } s; return 42; }' struct-anonymous
check 42 'int main(void) { struct S { int x, y; char *p, bytes[3]; }; struct S s; return 42; }' struct-multiple-members
check 42 'struct S { struct { int x; } inner; long a[2]; }; int main(void) { struct S s; return 42; }' struct-nested-members
check 42 'struct S { int x; }; int main(void) { int S = 42; struct S s; return S; }' struct-separate-namespace
check 16 'struct S { char c; long n; }; int main(void) { struct S a[2]; void *v = a; char *p = v; v = a + 1; char *q = v; return q - p; }' struct-padding-and-alignment
check 16 'struct S { long n; char c; }; int main(void) { struct S a[2]; void *v = a; char *p = v; v = a + 1; char *q = v; return q - p; }' struct-tail-padding
check 24 'struct Inner { int x; char c; }; struct S { char c; struct Inner inner; long n; }; int main(void) { struct S a[2]; void *v = a; char *p = v; v = a + 1; char *q = v; return q - p; }' struct-nested-layout
check 42 'struct S { int x; }; struct S *id(struct S *p) { return p; } int main(void) { struct S s; return id(&s) == &s ? 42 : 0; }' struct-pointer-function
check 42 'struct S; int f(struct S *p) { return p == 0 ? 42 : 0; } int main(void) { return f(0); }' struct-incomplete-pointer-parameter
check 42 'struct S { int x; }; int main(void) { struct S s; struct S *p = &s; { struct S { char c; }; struct S t; } struct S *q = &s; return p == q ? 42 : 0; }' struct-tag-shadowing
check 42 'int main(void) { { struct S { int x; }; } { struct S { char c; }; } return 42; }' struct-sibling-scopes
check 42 'int main(void) { for (struct S { int x; } *p = 0; p; ) {} struct S { char c; }; return 42; }' struct-for-scope
check 42 'int f(struct S { int x; } *p) { struct S s; return p == 0 ? 42 : 0; } int main(void) { struct S { char c; }; return f(0); }' struct-parameter-tag-scope

check_error struct-redefinition 'struct S { int x; }; struct S { int y; };' '1:29: error: redefinition of struct '\''S'\'''
check_error struct-recursive-definition 'struct S { struct S { int x; } member; };' '1:19: error: redefinition of struct '\''S'\'''
check_error struct-empty 'struct S {};' '1:11: error: empty structs are not supported'
check_error struct-duplicate-member 'struct S { int x; char x; };' '1:24: error: duplicate struct member'
check_error struct-void-member 'struct S { void x; };' '1:17: error: member has void type'
check_error struct-incomplete-member 'struct S { struct Missing x; };' '1:27: error: member has incomplete type'
check_error struct-self-value-member 'struct S { struct S next; };' '1:21: error: member has incomplete type'
check_error struct-incomplete-array-member 'struct S { struct Missing x[2]; };' '1:28: error: array element has incomplete type'
check_error struct-missing-member-name 'struct S { int; };' '1:15: error: expect member name'
check_error struct-missing-member-semicolon 'struct S { int x };' '1:17: error: expect '\'';'\'''
check_error struct-missing-brace 'struct S { int x;' '1:18: error: expect '\''}'\'''
check_error struct-missing-tag-or-body 'struct;' '1:7: error: expect struct tag or '\''{'\'''
check_error struct-bit-field 'struct S { int x:3; };' '1:17: error: expect '\'';'\'''
check_error struct-flexible-array 'struct S { int x; char data[]; };' '1:29: error: array bound is required'
check_error struct-member-initializer 'struct S { int x = 1; };' '1:17: error: expect '\'';'\'''
check_error struct-incomplete-local 'int main(void) { struct S s; return 0; }' '1:27: error: variable has incomplete type'
check_error struct-incomplete-array 'int main(void) { struct S a[2]; return 0; }' '1:28: error: array element has incomplete type'
check_error struct-array-overflow 'struct S { long x[268435455]; long y[2]; };' '1:36: error: struct size exceeds supported limit'
check_error struct-tail-padding-overflow 'struct S { long x; char a[2147483639]; };' '1:1: error: struct size exceeds supported limit'
check_error struct-initializer 'struct S { int x; }; int main(void) { struct S s = 0; return 0; }' '1:50: error: struct initializers are not supported yet'
check_error struct-return-by-value 'struct S { int x; }; struct S f(void);' '1:31: error: returning structs by value is not supported yet'
check_error struct-parameter-by-value 'struct S { int x; }; int f(struct S s);' '1:37: error: passing structs by value is not supported yet'
check_error struct-pointer-type-mismatch 'struct A { int x; }; struct B { int x; }; int main(void) { struct A *a; struct B *b = a; return 0; }' '1:83: error: incompatible initializer type'
check_error struct-anonymous-type-identity 'int main(void) { struct { int x; } a; struct { int x; } *p = &a; return 0; }' '1:58: error: incompatible initializer type'
check_error struct-local-tag-outside-block 'int main(void) { { struct S { int x; }; } struct S s; return 0; }' '1:52: error: variable has incomplete type'
check_error struct-function-tag-scope 'int f(void) { struct S { int x; }; return 0; } int main(void) { struct S s; return 0; }' '1:74: error: variable has incomplete type'
check_error struct-forward-shadow 'struct S { int x; }; int main(void) { struct S; struct S s; return 0; }' '1:58: error: variable has incomplete type'
check_error struct-prototype-tag-scope 'int f(struct S *p); struct S { int x; }; int f(struct S *p);' '1:46: error: conflicting declaration of function '\''f'\'''
check_error struct-incomplete-pointer-add 'int main(void) { struct S *p; p + 1; return 0; }' '1:33: error: pointer arithmetic on incomplete type'
check_error struct-incomplete-pointer-increment 'int main(void) { struct S *p; p++; return 0; }' '1:32: error: pointer arithmetic on incomplete type'
check_error struct-incomplete-pointer-compound 'int main(void) { struct S *p; p += 1; return 0; }' '1:33: error: pointer arithmetic on incomplete type'
check_error struct-assignment-by-value 'struct S { int x; }; int main(void) { struct S a; struct S b; a = b; return 0; }' '1:67: error: struct values are not supported yet'
check_error struct-unprototyped-call 'int f(); struct S { int x; }; int main(void) { struct S a; return f(a); }' '1:69: error: passing structs by value is not supported yet'
check_error struct-condition 'struct S { int x; }; int main(void) { struct S a; if (a) return 1; return 0; }' '1:55: error: struct values are not supported yet'
check_error struct-prefix-increment 'struct S { int x; }; int main(void) { struct S a; ++a; return 0; }' '1:51: error: invalid increment target'
check_error struct-comparison 'struct S { int x; }; int main(void) { struct S a; return a == a; }' '1:58: error: struct values are not supported yet'

# Struct member addresses reuse scalar loads, stores, and pointer arithmetic.
for spec in 'char' 'unsigned char' 'short' 'unsigned short' 'int' 'unsigned' 'long' 'unsigned long' 'long long' 'unsigned long long'; do
    check 42 "struct S { char before; $spec value; char after; }; int main(void) { struct S s; struct S *p = &s; s.before = 7; s.after = 9; s.value = 20; p->value = p->value + 22; return s.before == 7 && s.after == 9 ? s.value : 0; }" "member-load-store-$spec"
done
check 42 'struct Point { int x; int y; }; int main(void) { struct Point p; struct Point *ptr = &p; p.x = 20; ptr->y = 22; return p.x + p.y; }' member-dot-and-arrow
check 1 'struct S { char a; unsigned char b; short c; unsigned short d; int e; unsigned f; }; int main(void) { struct S s; s.a = 255; s.b = 511; s.c = 65535; s.d = 131071; s.e = -1; s.f = 4294967295U; return s.a == -1 && s.b == 255 && s.c == -1 && s.d == 65535 && s.e < 0 && s.f > 0; }' member-narrow-and-signed-values
check 1 'struct S { long x; unsigned long y; long long z; unsigned long long w; }; int main(void) { struct S s; s.x = -4294967296L; s.y = 18446744073709551615UL; s.z = 1LL << 40; s.w = 1ULL << 63; return s.x < 0 && s.y >> 63 == 1 && s.z >> 40 == 1 && s.w >> 63 == 1; }' member-wide-values
check 42 'struct I { char c; int x; }; struct O { char c; struct I inner; }; int main(void) { struct O o; struct O *p = &o; o.c = 1; p->inner.c = 2; o.inner.x = 39; return o.c + o.inner.c + p->inner.x; }' member-nested-struct
check 42 'struct I { int x; }; struct O { char c; struct I *inner; }; int main(void) { struct I i; struct O o; struct O *p = &o; p->inner = &i; p->inner->x = 42; return o.inner->x; }' member-arrow-chain
check 42 'struct Node { int value; struct Node *next; }; int main(void) { struct Node a; struct Node b; a.value = 20; b.value = 22; a.next = &b; b.next = 0; struct Node *p = &a; int sum = 0; while (p) { sum += p->value; p = p->next; } return sum; }' member-linked-list
check 42 'struct S { char c; int values[3]; }; int main(void) { struct S s; struct S *p = &s; s.values[0] = 7; p->values[1] = 11; s.values[2] = 24; return s.values[0] + p->values[1] + s.values[2]; }' member-array-elements
check 42 'struct S { int x; }; struct O { char c; struct S items[2]; }; int main(void) { struct O o; o.items[0].x = 7; o.items[1].x = 35; return o.items[0].x + o.items[1].x; }' member-array-of-struct
check 42 'struct S { int x; }; int main(void) { struct S a[2]; a[0].x = 20; a[1].x = 22; struct S *p = a; return p->x + (p + 1)->x; }' member-struct-array-base
check 42 'struct S { char c; int x; }; int main(void) { struct S s; int *p = &s.x; *p = 42; return (&s)->x; }' member-address-of-scalar
check 42 'struct I { int x; }; struct O { char c; struct I inner; }; int main(void) { struct O o; struct I *p = &o.inner; p->x = 42; return o.inner.x; }' member-address-of-struct
check 42 'struct S { int values[2]; }; int main(void) { struct S s; int *p = s.values; p[1] = 42; return (*(&s.values))[1]; }' member-array-decay-and-address
check 1 'struct S { char c; int x; short y; }; int main(void) { struct S s; void *v = &s; char *base = v; v = &s.x; char *x = v; v = &s.y; char *y = v; return x - base == 4 && y - base == 8; }' member-layout-offsets
check 1 'struct I { char c; long n; }; struct O { char c; struct I inner; char end; }; int main(void) { struct O o; void *v = &o; char *base = v; v = &o.inner.n; char *n = v; v = &o.end; char *end = v; return n - base == 16 && end - base == 24; }' member-nested-layout-offsets
check 1 'struct S { char c; unsigned char u; short s; unsigned short t; int i; unsigned j; long l; unsigned long long q; }; int main(void) { struct S x; x.c = 126; x.u = 254; x.s = 100; x.t = 65534; x.i = 40; x.j = 40; x.l = 1L << 40; x.q = 1ULL << 63; return x.c++ == 126 && ++x.c == -128 && x.u++ == 254 && ++x.u == 0 && --x.s == 99 && x.t-- == 65534 && x.i++ == 40 && ++x.j == 41 && x.l++ == 1L << 40 && x.q-- == 1ULL << 63; }' member-increment-decrement
check 42 'struct S { int x; int *p; }; int main(void) { struct S s; int a[2]; a[1] = 42; s.p = a; s.p++; return *s.p; }' member-pointer-increment
check 1 'struct S { char x; }; int main(void) { struct S s; return (s.x = 255) == -1 && s.x == -1; }' member-assignment-result
check 1 'struct S { int x; }; int main(void) { struct S s; struct S t; s.x = t.x = 42; return s.x == 42 && t.x == 42; }' member-chained-assignment
check 1 'struct S { int x; }; int main(void) { struct S a[2]; int i = 0; a[i++].x = 41; i = 0; int old = a[i++].x++; return old == 41 && i == 1 && a[0].x == 42; }' member-index-evaluated-once
check 1 'struct S { int x; }; struct S *get(struct S *p, int *n) { (*n)++; return p; } int main(void) { struct S s; int n = 0; get(&s, &n)->x = 41; int old = get(&s, &n)->x++; return n == 2 && old == 41 && s.x == 42; }' member-call-base-evaluated-once
check 42 'struct S { char c; int x; }; int value(void) { return 42; } struct S *get(struct S *p) { return p; } int main(void) { struct S s; get(&s)->x = value(); return s.x; }' member-call-base-and-rhs
check 42 'struct S { int x; }; int read(struct S *p) { return p->x; } int main(void) { struct S s; s.x = 42; return read(&s); }' member-pointer-parameter
check 42 'struct S { int x; }; int last(int a, int b, int c, int d, int e, int f, int g) { return g; } int main(void) { struct S s; s.x = 42; return last(s.x, s.x, s.x, s.x, s.x, s.x, s.x); }' member-stack-call-argument
check 42 'struct S { char *text; }; int main(void) { struct S s; s.text = "*"; return s.text[0]; }' member-string-pointer
check 42 'int main(void) { struct { int x; } s; s.x = 42; return s.x; }' member-anonymous-struct
check 42 'struct S { int x; }; int main(void) { struct S s; struct S t; s.x = 7; t.x = 42; int choose = 0; return (choose ? &s : &t)->x; }' member-conditional-pointer
check 42 'struct S { int x; }; int main(void) { struct S s; s.x = 42; s.x; return (*(&s)).x; }' member-discarded-scalar

# Struct member access syntax and operand checks.
check_error member-dot-integer 'int main(void) { int x; return x.y; }' '1:33: error: '\''.'\'' requires a struct operand'
check_error member-dot-pointer 'struct S { int x; }; int main(void) { struct S *p; return p.x; }' '1:60: error: '\''.'\'' requires a struct operand'
check_error member-dot-array 'struct S { int x; }; int main(void) { struct S a[2]; return a.x; }' '1:62: error: '\''.'\'' requires a struct operand'
check_error member-arrow-integer 'int main(void) { int x; return x->y; }' '1:33: error: '\''->'\'' requires a pointer to struct'
check_error member-arrow-integer-pointer 'int main(void) { int *p; return p->x; }' '1:34: error: '\''->'\'' requires a pointer to struct'
check_error member-arrow-void-pointer 'int main(void) { void *p; return p->x; }' '1:35: error: '\''->'\'' requires a pointer to struct'
check_error member-arrow-struct 'struct S { int x; }; int main(void) { struct S s; return s->x; }' '1:58: error: struct values are not supported yet'
check_error member-arrow-pointer-to-pointer 'struct S { int x; }; int main(void) { struct S **p; return p->x; }' '1:61: error: '\''->'\'' requires a pointer to struct'
check_error member-incomplete-struct 'int main(void) { struct S *p; return p->x; }' '1:39: error: member access on incomplete struct'
check_error member-missing-dot 'struct S { int x; }; int main(void) { struct S s; return s.y; }' '1:60: error: no member named '\''y'\'' in struct'
check_error member-missing-arrow 'struct S { int x; }; int main(void) { struct S *p; return p->y; }' '1:62: error: no member named '\''y'\'' in struct'
check_error member-name-after-dot 'struct S { int x; }; int main(void) { struct S s; return s.; }' '1:60: error: expect member name'
check_error member-name-after-arrow 'struct S { int x; }; int main(void) { struct S *p; return p->; }' '1:62: error: expect member name'
check_error member-keyword-name 'struct S { int x; }; int main(void) { struct S s; return s.int; }' '1:60: error: expect member name'
check_error member-scalar-chain 'struct S { int x; }; int main(void) { struct S s; return s.x.y; }' '1:61: error: '\''.'\'' requires a struct operand'
check_error member-array-assignment 'struct S { int a[2]; }; int main(void) { struct S s; s.a = 0; return 0; }' '1:58: error: invalid assignment target'
check_error member-array-increment 'struct S { int a[2]; }; int main(void) { struct S s; s.a++; return 0; }' '1:57: error: invalid increment target'
check_error member-struct-increment 'struct T { int x; }; struct S { struct T t; }; int main(void) { struct S s; ++s.t; return 0; }' '1:77: error: invalid increment target'
check_error member-pointer-type 'struct S { char *p; }; int main(void) { struct S s; int *p = s.p; return 0; }' '1:58: error: incompatible initializer type'
check_error member-array-address-type 'struct S { int a[2]; }; int main(void) { struct S s; int *p = &s.a; return 0; }' '1:59: error: incompatible initializer type'
check 42 'struct S { int x; }; int main(void) { struct S s; s.x = 40; s.x += 2; return s.x; }' member-compound-assignment
check_error member-address-rvalue 'struct S { int x; }; int main(void) { struct S s; int *p = &(s.x + 1); return 0; }' '1:60: error: invalid address-of target'

# String literals have static storage and decay to pointers in value contexts.
check 97 'int main(void) { return "abc"[0]; }' string-first-byte
check 99 'int main(void) { return 2["abc"]; }' string-reversed-subscript
check 0 'int main(void) { return "abc"[3]; }' string-final-terminator
check 0 'int main(void) { return ""[0]; }' string-empty
check 1 'int main(void) { char *p = "a\0b"; return p[0] == 97 && p[1] == 0 && p[2] == 98 && p[3] == 0; }' string-embedded-zero
check 1 'int main(void) { char *p = "\n\t\r\a\b\f\v"; return p[0] == 10 && p[1] == 9 && p[2] == 13 && p[3] == 7 && p[4] == 8 && p[5] == 12 && p[6] == 11 && p[7] == 0; }' string-simple-escapes
check 1 'int main(void) { char *p = "\"\\\?"; return p[0] == 34 && p[1] == 92 && p[2] == 63 && p[3] == 0; }' string-escaped-punctuation
check 1 'int main(void) { char *p = "\101\x42\377\x80"; return p[0] == 65 && p[1] == 66 && p[2] == -1 && p[3] == -128 && p[4] == 0; }' string-numeric-escapes
check 1 'int main(void) { char *p = "ab" /* concatenate */ "cd"; return p[0] == 97 && p[2] == 99 && p[3] == 100 && p[4] == 0; }' string-concatenation
check 1 'int main(void) { char *p = "a\0" "b"; return p[0] == 97 && p[1] == 0 && p[2] == 98 && p[3] == 0; }' string-concatenated-zero
check 1 'int main(void) { char *p = "\x4" "1"; return p[0] == 4 && p[1] == 49 && p[2] == 0; }' string-concatenated-escape-boundary
check 1 'int main(void) { char *p = "" "" "a" ""; return p[0] == 97 && p[1] == 0; }' string-concatenated-empty
check 1 'int main(void) { char *p = "0123456789abcdefXYZ"; return p[15] == 102 && p[16] == 88 && p[18] == 90 && p[19] == 0; }' string-multiple-byte-directives
check 98 'int main(void) { char *p; p = "abc"; return *(p + 1); }' string-pointer-assignment
check 98 'int main(void) { return (*(&"abc"))[1]; }' string-address-of-array
check 99 'int main(void) { return (*(&"abc" + 1 - 1))[2]; }' string-pointer-to-array-arithmetic
check 98 'int byte(char *p) { return p[1]; } int main(void) { return byte("abc"); }' string-call-argument
check 98 'int byte(int a, int b, int c, int d, int e, int f, char *p) { return p[1]; } int main(void) { return byte(0,0,0,0,0,0,"abc"); }' string-stack-argument
check 1 'char *text(void) { return "abc"; } int main(void) { char *p = text(); int i = 0; while (i++ < 3) { if (text() != p) return 0; } return p[2] == 99; }' string-static-lifetime
check 1 'char *first(void) { return "abc"; } char *second(void) { return "xyz"; } int main(void) { return first()[0] == 97 && second()[0] == 120; }' string-labels-across-functions
check 98 'int main(void) { char *p[2]; p[0] = "abc"; p[1] = "xyz"; return p[0][1]; }' string-pointer-array
check 120 'int main(void) { return (0 ? "abc" : "xyz")[0]; }' string-conditional
check 1 'int main(void) { if ("") return !!"abc" && ("x" || 0) && ("y" && 1); return 0; }' string-conditions
check 98 'int main(void) { "discarded"; return (0, "abc")[1]; }' string-discarded-expression
check 1 'int strcmp(char *a, char *b); int main(void) { return strcmp("hello", "hel" "lo") == 0 && strcmp("a", "b") < 0; }' string-libc-call

# Array expressions, element access, and pointer conversion.
for spec in 'char' 'unsigned char' 'short' 'unsigned short' 'int' 'unsigned' 'long' 'unsigned long' 'long long' 'unsigned long long'; do
    check 42 "int main(void) { $spec a[3]; a[0] = 7; a[1] = 11; a[2] = 24; return a[0] + a[1] + a[2]; }" "array-elements-$spec"
done
check 1 'int main(void) { char a[2]; a[0] = 255; a[1] = 42; return a[0] == -1 && a[1] == 42; }' array-signed-char
check 1 'int main(void) { unsigned char a[2]; a[0] = 511; a[1] = 42; return a[0] == 255 && a[1] == 42; }' array-unsigned-char
check 1 'int main(void) { short a[2]; a[0] = 65535; a[1] = 42; return a[0] == -1 && a[1] == 42; }' array-signed-short
check 1 'int main(void) { unsigned short a[2]; a[0] = 131071; a[1] = 42; return a[0] == 65535 && a[1] == 42; }' array-unsigned-short
check 1 'int main(void) { long long a[2]; a[0] = 1LL << 40; a[1] = -1; return a[0] >> 40 == 1 && a[1] < 0; }' array-wide-values
check 1 'int main(void) { unsigned long long a[2]; a[0] = 18446744073709551615ULL; a[1] = 1ULL << 63; return a[0] / 2 == 9223372036854775807LL && a[1] >> 63 == 1; }' array-unsigned-wide-values
check 42 'int main(void) { int a[3]; int *p = a; *(p + 2) = 42; return a[2]; }' array-pointer-initializer
check 42 'int main(void) { int a[3]; int *p; p = a; p[1] = 42; return *(a + 1); }' array-pointer-assignment
check 42 'int main(void) { int a[3]; 2[a] = 42; return a[2]; }' array-reversed-subscript
check 42 'int main(void) { int a[3]; a[0] = 42; int *p = a + 2; long n = -2; return p[n]; }' array-negative-index
check 42 'int main(void) { int a[3]; unsigned long n = 2; a[n] = 42; return a[2U]; }' array-unsigned-index
check 1 'int main(void) { int a[3]; return a + 3 - a == 3 && (&a + 1) - &a == 1; }' array-pointer-scaling
check 42 'int main(void) { int a[3]; (&a)[0][2] = 42; return (*(&a))[2]; }' array-address-preserves-type
check 42 'int main(void) { int a[2]; int *p = &a[1]; *p = 42; return a[1]; }' array-element-address
check 42 'int main(void) { int a[2]; int *p = (a); p[0] = 42; return (*(&(a)))[0]; }' array-parenthesized-address
check 1 'int main(void) { int a[2]; a[0] = 40; a[1] = 42; return a[0]++ == 40 && ++a[0] == 42 && a[1]-- == 42 && --a[1] == 40; }' array-element-increment
check 1 'int main(void) { int a[2]; int i = 0; a[i++] = 41; i = 0; return a[i++]++ == 41 && i == 1 && a[0] == 42; }' array-index-evaluated-once
check 42 'int *base(int *p, int *n) { (*n)++; return p; } int main(void) { int a[2]; int n = 0; base(a, &n)[1] = 42; return n == 1 ? a[1] : 0; }' array-base-evaluated-once
check 42 'int main(void) { int a[2]; a[0] = 7; a[1] = 42; int i = 0; return a[(i = 1, i)]; }' array-comma-index
check 42 'int sum(int a[3]) { return a[0] + a[1] + a[2]; } int main(void) { int a[3]; a[0] = 7; a[1] = 11; a[2] = 24; return sum(a); }' array-call-argument
check 42 'int f(); int main(void) { int a[1]; a[0] = 42; return f(a); } int f(int *p) { return p[0]; }' array-default-argument-conversion
check 42 'int f(int x, int y, int z, int u, int v, int w, int *p) { return p[0]; } int main(void) { int a[1]; a[0] = 42; return f(0,0,0,0,0,0,a); }' array-stack-argument
check 42 'int *id(int *p) { return p; } int main(void) { int a[2]; a[1] = 42; return id(a)[1]; }' array-subscript-call-result
check 42 'void *id(void *p) { return p; } int main(void) { int a[1]; a[0] = 42; int *p = id(a); return *p; }' array-void-pointer-conversion
check 42 'int main(void) { int a[2]; int b[2]; a[0] = 1; b[0] = 42; return (0 ? a : b)[0]; }' array-conditional-conversion
check 42 'int main(void) { int a[2]; a[0] = 42; return (0, a)[0]; }' array-comma-conversion
check 1 'int main(void) { int a[2]; return a == &a[0] && a != 0 && !!a && (a && a) && (0 || a); }' array-comparisons-and-logical
check 42 'int main(void) { int a[1]; a[0] = 42; if (a) return a[0]; return 0; }' array-if-condition
check 42 'int main(void) { char a[1]; a[0] = 42; return a ? a[0] : 0; }' array-conditional-condition
check 42 'int main(void) { int a[1]; a[0] = 42; while (a) return a[0]; return 0; }' array-while-condition
check 42 'int main(void) { int a[1]; a[0] = 42; for (; a;) return a[0]; return 0; }' array-for-condition
check 1 'int main(void) { int a[1]; int n = 0; do { n++; } while (n < 2 && a); return n == 2; }' array-do-condition
check 1 'int main(void) { int a[1]; int n = 0; do { if (++n == 2) return 1; } while (a); return 0; }' array-do-direct-condition
check 42 'int main(void) { int a[2]; int b[2]; int *p[2]; p[0] = a; p[1] = b; p[1][1] = 42; return b[1]; }' array-pointer-elements
check 42 'int main(void) { int a[2]; int *p[1]; p[0] = a; (*p)[1] = 42; return a[1]; }' array-pointer-element-deref
check 42 'int main(void) { int a[2]; a[0] = 42; { int a[1]; a[0] = 7; } return a[0]; }' array-shadowing
check 42 'int main(void) { char c = 7; int a[3]; char d = 9; long b[2]; a[2] = 12; b[1] = 14; return c + a[2] + d + b[1]; }' array-storage-separation
check 42 'int main(void) { int a[1]; a; *(&a); a[0] = 42; return a[0]; }' array-discarded-expression

check_error array-assignment 'int main(void) { int a[2]; int b[2]; a = b; return 0; }' '1:40: error: invalid assignment target'
check_error array-indirect-assignment 'int main(void) { int a[2]; (*(&a)) = a; return 0; }' '1:36: error: invalid assignment target'
check_error array-prefix-increment 'int main(void) { int a[2]; ++a; return 0; }' '1:28: error: invalid increment target'
check_error array-postfix-decrement 'int main(void) { int a[2]; a--; return 0; }' '1:29: error: invalid decrement target'
check_error array-compound-assignment 'int main(void) { int a[2]; a += 1; return 0; }' '1:30: error: invalid assignment target'
check 42 'int main(void) { int a[2]; a[0] = 40; a[0] += 2; return a[0]; }' array-compound-element-assignment
check_error array-invalid-subscript 'int main(void) { int a[2]; int b[2]; return a[b]; }' '1:46: error: subscript requires a pointer and an integer'
check_error array-integer-subscript 'int main(void) { return 1[2]; }' '1:26: error: subscript requires a pointer and an integer'
check_error array-void-subscript 'void f(void); int main(void) { int a[2]; return a[f()]; }' '1:50: error: subscript requires a pointer and an integer'
check_error array-subscript-missing-rbracket 'int main(void) { int a[2]; return a[0; }' '1:38: error: expect '\'']'\'''
check_error array-invalid-product 'int main(void) { int a[2]; return a * 2; }' '1:37: error: invalid operands to integer operator'
check_error array-invalid-negation 'int main(void) { int a[2]; return -a; }' '1:35: error: invalid operand to unary -'
check_error array-invalid-complement 'int main(void) { int a[2]; return ~a; }' '1:35: error: invalid operand to unary ~'
check_error array-invalid-return 'int main(void) { int a[2]; return a; }' '1:28: error: incompatible return type'
check_error array-pointer-to-array-mismatch 'int main(void) { int a[2]; int *p = &a; return 0; }' '1:33: error: incompatible initializer type'
check_error array-call-type-mismatch 'int f(char *p); int main(void) { int a[2]; return f(a); }' '1:53: error: incompatible argument type'
check_error array-incompatible-subtraction 'int main(void) { int a[2]; char b[2]; return a - b; }' '1:48: error: pointer subtraction requires compatible types'

# Long long literal types and boundaries.
check 1 'int main(void) { return 1LL << 40 == 1099511627776L; }' literal-long-long-width
check 1 'int main(void) { return 1LL - 2 < 0 && 0ll == 0; }' literal-long-long-signed
check 1 'int main(void) { return 1ULL - 2 > 0 && 0llu - 1 == 18446744073709551615ULL; }' literal-unsigned-long-long-type
check 1 'int main(void) { long long x = 9223372036854775807LL; return x >> 62 == 1 && x > 2147483647; }' literal-long-long-max
check 1 'int main(void) { long long x = -9223372036854775807LL - 1; return x >> 63 == -1 && x < 0; }' literal-long-long-min
check 1 'int main(void) { unsigned long long x = 18446744073709551615ULL; return x / 2 == 9223372036854775807LL && x + 1 == 0; }' literal-unsigned-long-long-max
check 1 'int main(void) { return 037777777777LL > -1 && 040000000000LL >> 32 == 1; }' literal-octal-long-long-small
check 1 'int main(void) { return 0777777777777777777777LL == 9223372036854775807LL; }' literal-octal-long-long-max
check 1 'int main(void) { return 01000000000000000000000LL >> 63 == 1 && 01000000000000000000000LL > 0; }' literal-octal-long-long-unsigned-fallback
check 1 'int main(void) { return 01777777777777777777777ll == 18446744073709551615ULL; }' literal-octal-long-long-unsigned-max
check 1 'int main(void) { return -1LL < 1U && -1LL > 1UL && -1L > 1ULL; }' literal-long-long-mixed-signedness
check 1 'int main(void) { return (1 ? -1LL : 1U) < 0 && (1 ? -1LL : 1UL) > 0; }' literal-long-long-conditional-type
check 1 'int main(void) { unsigned u = 18446744073709551615ULL; short s = 65535LL; return u == 4294967295U && s == -1; }' literal-long-long-narrow-conversions
check 1 'long long f(long long x) { return x; } int main(void) { return f(9223372036854775807LL) == 9223372036854775807LL; }' literal-long-long-call
check 1 'unsigned long long f(int a, int b, int c, int d, int e, int f, unsigned long long g) { return g; } int main(void) { return f(1,2,3,4,5,6,18446744073709551615ULL) == 18446744073709551615ULL; }' literal-long-long-stack-argument
check 42 'int main(void) { switch (9223372036854775807LL) { case 9223372036854775807LL: return 42; default: return 0; } }' case-long-long-max
check 42 'int main(void) { switch (-9223372036854775807LL - 1) { case -9223372036854775807LL - 1: return 42; default: return 0; } }' case-long-long-min
check 42 'int main(void) { switch (0) { case 18446744073709551615ULL + 1: return 42; default: return 0; } }' case-long-long-wraparound
check 42 'int main(void) { switch (1) { case -1LL < 1U: return 42; default: return 0; } }' case-long-long-mixed-signedness
check 1 'int main(void) { int *p = 18446744073709551615ULL + 1; return p == 0; }' literal-long-long-null-constant
check_error literal-over-signed-long-long 'int main(void) { return 9223372036854775808LL; }' '1:25: error: integer literal out of range for supported signed types'
check_error literal-negative-over-signed-long-long 'int main(void) { return -9223372036854775808ll; }' '1:26: error: integer literal out of range for supported signed types'
check_error literal-over-uint64-long-long 'int main(void) { return 18446744073709551616ULL; }' '1:25: error: integer literal out of range for 64-bit unsigned integer'
check_error literal-over-uint64-octal-long-long 'int main(void) { return 02000000000000000000000LL; }' '1:25: error: integer literal out of range for 64-bit unsigned integer'
check_error case-long-long-overflow-add 'int main(void) { switch (0) { case 9223372036854775807LL + 1LL: return 1; } return 0; }' '1:63: error: integer overflow in constant expression'
check_error case-long-long-overflow-div 'int main(void) { switch (0) { case (-9223372036854775807LL - 1) / -1LL: return 1; } return 0; }' '1:71: error: integer overflow in constant expression'
check_error case-long-long-duplicate 'int main(void) { switch (0ULL) { case -1LL: return 1; case 18446744073709551615ULL: return 2; } return 0; }' '1:55: error: duplicate case value'

# Integer literal ranges, suffix types, and 64-bit constant expressions.
check 1 'int main(void) { return 2147483648 > -1; }' literal-decimal-above-int
check 1 'int main(void) { return 4294967295 > -1 && ~4294967295 == -4294967296L; }' literal-decimal-uses-long
check 1 'int main(void) { return -2147483648 < 0U; }' literal-minus-is-separate-operator
check 1 'int main(void) { return 1U - 2 > 0 && 4294967295U + 1 == 0; }' literal-unsigned-int-type
check 1 'int main(void) { return 4294967296U >> 32 == 1 && 4294967296U > -1L == 0; }' literal-unsigned-promotes-to-long
check 1 'int main(void) { return 1L << 40 == 1099511627776; }' literal-long-suffix-width
check 1 'int main(void) { return 1UL - 2 > 9223372036854775807L; }' literal-unsigned-long-suffix
check 1 'int main(void) { return 037777777777 > -1 == 0 && ~037777777777 == 0U; }' literal-octal-unsigned-int-type
check 1 'int main(void) { return 040000000000 > -1 && 040000000000 >> 32 == 1; }' literal-octal-long-type
check 1 'int main(void) { return 037777777777L > -1 && 037777777777L >> 32 == 0; }' literal-octal-long-suffix
check 1 'int main(void) { return 01000000000000000000000 >> 63 == 1; }' literal-octal-unsigned-long-type
check 1 'int main(void) { return 01777777777777777777777 == 18446744073709551615UL; }' literal-octal-unsigned-long-max
check 1 'int main(void) { return 01000000000000000000000L > 0; }' literal-octal-long-selects-unsigned
check 1 'int main(void) { long x = 9223372036854775807; return x > 2147483647 && x >> 62 == 1; }' literal-long-max
check 1 'int main(void) { long x = -9223372036854775807L - 1; return x < 0 && x >> 63 == -1; }' literal-long-min
check 1 'int main(void) { unsigned long x = 18446744073709551615U; return x / 2 == 9223372036854775807L && x + 1 == 0; }' literal-unsigned-long-max
check 1 'int main(void) { return 18446744073709551615ul == 18446744073709551615LU; }' literal-suffix-case-order-large
check 1 'int main(void) { unsigned short s = 4294967295U; unsigned u = 18446744073709551615UL; return s == 65535 && u == 4294967295U; }' literal-narrow-conversions
check 1 'long f(long x) { return x; } int main(void) { return f(4294967296L) == 4294967296L; }' literal-long-call
check 1 'unsigned long f(int a, int b, int c, int d, int e, int f, unsigned long g) { return g; } int main(void) { return f(1,2,3,4,5,6,18446744073709551615UL) == 18446744073709551615UL; }' literal-wide-stack-argument
check 42 'int main(void) { long x = 4294967296L; switch (x) { case 4294967296L: return 42; default: return 0; } }' case-long-literal
check 42 'int main(void) { unsigned long x = 18446744073709551615UL; switch (x) { case 18446744073709551615UL: return 42; default: return 0; } }' case-unsigned-long-literal
check 42 'int main(void) { long x = -9223372036854775807L - 1; switch (x) { case -9223372036854775807L - 1: return 42; default: return 0; } }' case-long-min
check 42 'int main(void) { switch (0) { case 18446744073709551615UL + 1: return 42; default: return 0; } }' case-unsigned-long-add-wrap
check 42 'int main(void) { unsigned long x = 18446744073709551615UL; switch (x) { case 0UL - 1: return 42; default: return 0; } }' case-unsigned-long-subtract-wrap
check 42 'int main(void) { switch (1) { case 18446744073709551615UL * 18446744073709551615UL: return 42; default: return 0; } }' case-unsigned-long-multiply-wrap
check 42 'int main(void) { long x = 9223372032559808512L; switch (x) { case 4294967296L * 2147483647: return 42; default: return 0; } }' case-long-multiply
check 42 'int main(void) { long x = -9223372036854775807L - 1; switch (x) { case (-9223372036854775807L - 1) * 1: return 42; default: return 0; } }' case-long-min-multiply
check 42 'int main(void) { long x = 9223372036854775807L; switch (x) { case 18446744073709551615UL / 2: return 42; default: return 0; } }' case-unsigned-long-division
check 42 'int main(void) { switch (1) { case 18446744073709551615UL % 7: return 42; default: return 0; } }' case-unsigned-long-remainder
check 42 'int main(void) { switch (-1) { case -4294967297L / 4294967296L: return 42; default: return 0; } }' case-long-negative-division
check 42 'int main(void) { long x = -9223372036854775807L - 1; switch (x) { case -9223372036854775807L + -1L: return 42; default: return 0; } }' case-long-add-negative
check 42 'int main(void) { switch (-1) { case (-9223372036854775807L - 1) + 9223372036854775807L: return 42; default: return 0; } }' case-long-add-min-max
check 42 'int main(void) { switch (0) { case 9223372036854775807L - 9223372036854775807L: return 42; default: return 0; } }' case-long-subtract
check 42 'int main(void) { unsigned long x = 9223372036854775808UL; switch (x) { case 1UL << 63: return 42; default: return 0; } }' case-long-shift-63
check 42 'int main(void) { switch (1) { case 18446744073709551615UL >> 63: return 42; default: return 0; } }' case-unsigned-long-right-shift
check 42 'int main(void) { switch (-1) { case -1L >> 63: return 42; default: return 0; } }' case-long-signed-right-shift
check 42 'int main(void) { long x = 1099511627776L; switch (x) { case 1L << 40: return 42; default: return 0; } }' case-long-left-shift
check 42 'int main(void) { switch (1) { case 18446744073709551615UL > 9223372036854775807L: return 42; default: return 0; } }' case-unsigned-long-comparison
check 42 'int main(void) { switch (0) { case 18446744073709551615UL < 1UL: return 42; default: return 0; } }' case-unsigned-long-comparison-false
check 42 'int main(void) { switch (-1) { case ~0UL: return 42; default: return 0; } }' case-unsigned-long-narrow
check 42 'int main(void) { switch (0) { case ~18446744073709551615UL: return 42; default: return 0; } }' case-unsigned-long-not
check 42 'int main(void) { switch (1) { case -18446744073709551615UL: return 42; default: return 0; } }' case-unsigned-long-negate
check 42 'int main(void) { switch (0) { case 0 && (9223372036854775807L + 1L): return 42; default: return 0; } }' case-long-overflow-short-circuit
check 1 'int main(void) { int *p = 18446744073709551615UL + 1; return p == 0; }' literal-unsigned-long-null-constant

check_error literal-over-uint64 'int main(void) { return 18446744073709551616UL; }' '1:25: error: integer literal out of range for 64-bit unsigned integer'
check_error literal-over-uint64-no-suffix 'int main(void) { return 18446744073709551616; }' '1:25: error: integer literal out of range for 64-bit unsigned integer'
check_error literal-over-uint64-octal 'int main(void) { return 02000000000000000000000U; }' '1:25: error: integer literal out of range for 64-bit unsigned integer'
check_error literal-over-signed-long 'int main(void) { return 9223372036854775808; }' '1:25: error: integer literal out of range for supported signed types'
check_error literal-over-signed-long-suffix 'int main(void) { return 9223372036854775808L; }' '1:25: error: integer literal out of range for supported signed types'
check_error case-long-overflow-add 'int main(void) { switch (0) { case 9223372036854775807L + 1L: return 1; } return 0; }' '1:61: error: integer overflow in constant expression'
check_error case-long-overflow-sub 'int main(void) { switch (0) { case (-9223372036854775807L - 1) - 1L: return 1; } return 0; }' '1:68: error: integer overflow in constant expression'
check_error case-long-overflow-mul 'int main(void) { switch (0) { case 9223372036854775807L * 2L: return 1; } return 0; }' '1:61: error: integer overflow in constant expression'
check_error case-long-overflow-mul-min 'int main(void) { switch (0) { case (-9223372036854775807L - 1) * -1L: return 1; } return 0; }' '1:69: error: integer overflow in constant expression'
check_error case-long-overflow-neg 'int main(void) { switch (0) { case -(-9223372036854775807L - 1): return 1; } return 0; }' '1:64: error: integer overflow in constant expression'
check_error case-long-overflow-div 'int main(void) { switch (0) { case (-9223372036854775807L - 1) / -1L: return 1; } return 0; }' '1:69: error: integer overflow in constant expression'
check_error case-long-overflow-mod 'int main(void) { switch (0) { case (-9223372036854775807L - 1) % -1L: return 1; } return 0; }' '1:69: error: integer overflow in constant expression'
check_error case-long-shift-64 'int main(void) { switch (0) { case 1L << 64: return 1; } return 0; }' '1:44: error: invalid shift count in constant expression'
check_error case-unsigned-long-shift-64 'int main(void) { switch (0) { case 1UL << 64: return 1; } return 0; }' '1:45: error: invalid shift count in constant expression'
check_error case-long-overflow-shift 'int main(void) { switch (0) { case 9223372036854775807L << 1: return 1; } return 0; }' '1:61: error: integer overflow in constant expression'
check_error case-long-shift-negative 'int main(void) { switch (0) { case -1L << 1: return 1; } return 0; }' '1:44: error: left shift of negative value in constant expression'
check_error case-unsigned-long-zero-divisor 'int main(void) { switch (0) { case 1UL / 0UL: return 1; } return 0; }' '1:45: error: division by zero in constant expression'
check_error case-long-duplicate-after-conversion 'int main(void) { switch (0) { case 4294967296L: return 1; case 0: return 2; } }' '1:59: error: duplicate case value'
check_error case-unsigned-long-duplicate 'int main(void) { unsigned long x = 0; switch (x) { case -1: return 1; case 18446744073709551615UL: return 2; } return 0; }' '1:71: error: duplicate case value'

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
check_error long-long-long-specifiers 'int main(void) { long long long x; return 0; }' '1:28: error: duplicate long type specifier'
check_error long-duplicate-int 'int main(void) { long int int x; return 0; }' '1:27: error: duplicate int type specifier'
check_error long-duplicate-unsigned 'int main(void) { unsigned long unsigned x; return 0; }' '1:32: error: duplicate unsigned type specifier'
check_error long-conflicting-return 'long f(void); int f(void);' '1:19: error: conflicting declaration of function '"'"'f'"'"''
check_error long-conflicting-parameter 'int f(long); int f(int);' '1:18: error: conflicting declaration of function '"'"'f'"'"''
check_error long-conflicting-signedness 'long f(long); unsigned long f(long);' '1:29: error: conflicting declaration of function '"'"'f'"'"''
check_error long-conflicting-parameter-signedness 'int f(long); int f(unsigned long);' '1:18: error: conflicting declaration of function '"'"'f'"'"''
check_error long-incompatible-pointer 'int main(void) { int x; long *p = &x; return 0; }' '1:31: error: incompatible initializer type'
check_error long-declaration-as-statement 'int main(void) { if (1) long x; return 0; }' '1:25: error: expect statement'

# Long long declaration specifiers, including nonadjacent long keywords.
for spec in 'long long' 'long long int' 'long int long' 'int long long' \
    'unsigned long long' 'long unsigned long' 'long long unsigned' \
    'unsigned int long long' 'unsigned long int long' 'unsigned long long int' \
    'int unsigned long long' 'int long unsigned long' 'int long long unsigned' \
    'long unsigned int long' 'long unsigned long int' 'long int unsigned long' \
    'long int long unsigned' 'long long unsigned int' 'long long int unsigned'; do
    check 42 "$spec f($spec); $spec f($spec x) { return x; } int main(void) { $spec x = 42; return f(x); }" "long-long-declspecs-$spec"
done
check 42 'int main(void) { for (long long i = 42; i;) return i; return 0; }' long-long-for-declaration
check 42 'int main(void) { for (unsigned long long i = 42; i;) return i; return 0; }' unsigned-long-long-for-declaration
check 42 'int main(void) { long long x = 42; long long *p = &x; return *p; }' long-long-pointer-declaration
check 1 'int main(void) { long long x = 1; x <<= 40; return x + x == 2199023255552L; }' long-long-common-type-width
check 1 'int main(void) { long long x = -1; unsigned y = 1; return x < y; }' long-long-unsigned-int-common-type
check 1 'int main(void) { long long x = -1; unsigned long y = 1; return x > y; }' long-long-unsigned-long-common-type
check 1 'int main(void) { long x = -1; unsigned long long y = 1; return x > y; }' long-unsigned-long-long-common-type
check_error long-long-short-specifiers 'int main(void) { long long short x; return 0; }' '1:28: error: invalid combination of type specifiers'
check_error long-long-char-specifiers 'int main(void) { long long char x; return 0; }' '1:28: error: invalid combination of type specifiers'
check_error long-long-duplicate-int 'int main(void) { long long int int x; return 0; }' '1:32: error: duplicate int type specifier'
check_error long-long-duplicate-unsigned 'int main(void) { unsigned long long unsigned x; return 0; }' '1:37: error: duplicate unsigned type specifier'
check_error long-long-conflicting-return 'long f(void); long long f(void);' '1:25: error: conflicting declaration of function '"'"'f'"'"''
check_error long-long-conflicting-parameter 'int f(long); int f(long long);' '1:18: error: conflicting declaration of function '"'"'f'"'"''
check_error long-long-conflicting-signedness 'int f(long long); int f(unsigned long long);' '1:23: error: conflicting declaration of function '"'"'f'"'"''
check_error long-long-incompatible-pointer 'int main(void) { long x; long long *p = &x; return 0; }' '1:37: error: incompatible initializer type'

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
check_error invalid-unsigned-void 'int main(void) { unsigned void x; return 0; }' '1:27: error: invalid combination of type specifiers'

# Diagnostic locations audited against GCC and Clang. Expected diagnostics
# check our filename, line, and byte column.
check_error location-return-semicolon $'int main(void) {\n  return 42\n}\n' '2:12: error: expect '\'';'\'''
check_error location-decl-semicolon $'int main(void) {\n  int a = 3\n  return a;\n}\n' '2:12: error: expect '\'';'\'''
check_error location-expression-semicolon $'int main(void) {\n  int a;\n  a = 3\n  return a;\n}\n' '3:8: error: expect '\'';'\'''
check_error location-semicolon-after-comment $'int main(void) {\n  return 42 /* comment\n  spans lines */\n}\n' '2:12: error: expect '\'';'\'''
check_error location-missing-member-semicolon $'struct S {\n  int x\n};\n' '2:8: error: expect '\'';'\'''
check_error location-break-semicolon $'int main(void) {\n  while (1) {\n    break\n  }\n}\n' '3:10: error: expect '\'';'\'''
check_error location-continue-semicolon $'int main(void) {\n  while (1) {\n    continue\n  }\n}\n' '3:13: error: expect '\'';'\'''
check_error location-goto-semicolon $'int main(void) {\n  L: ;\n  goto L\n}\n' '3:9: error: expect '\'';'\'''
check_error location-do-semicolon $'int main(void) {\n  do {} while (0)\n}\n' '2:18: error: expect '\'';'\'''
check_error location-string-token-end $'int main(void) {\n  char *p = "a\\n" "bc"\n  return 0;\n}\n' '2:23: error: expect '\'';'\'''
check_error location-return-keyword $'int main(void) {\n  return;\n}\n' '2:3: error: expect return expr'
check_error location-unnamed-parameter $'int f(\n  int,\n  char\n) { return 0; }' '2:3: error: expect parameter name in function definition'
check_error location-unnamed-array-parameter $'int f(\n  int [2]\n) { return 0; }' '2:3: error: expect parameter name in function definition'
check_error location-missing-array-variable-name $'int main(void) {\n  int [3];\n}\n' '2:7: error: expect variable name'
check_error location-missing-array-member-name $'struct S {\n  int [3];\n};\n' '2:7: error: expect member name'
check_error location-missing-pointer-name $'int main(void) {\n  int *;\n}\n' '2:8: error: expect variable name'
check_error location-bad-parameter $'int f(\n  int +\n) { return 0; }' '2:7: error: expect parameter name'
check_error location-expression-lf $'int main(void) {\n  return 1 +;\n}\n' '2:13: error: expect expr'
check_error location-expression-crlf $'int main(void) {\r\n  return 1 +;\r\n}\r\n' '2:13: error: expect expr'
check_error location-expression-cr $'int main(void) {\r  return 1 +;\r}\r' '2:13: error: expect expr'
check_error location-expression-tab $'int main(void) {\n\treturn 1 +;\n}\n' '2:12: error: expect expr'
check_error location-expression-utf8-comment $'int main(void) {\n  /* 中文 */ return 1 +;\n}\n' '2:26: error: expect expr'
check_error location-expression-comment $'int main(void) {\n  /* comment\n  end */ return 1 +;\n}\n' '3:20: error: expect expr'
check_error location-comment-start $'int main(void) {\n  /* broken\n  comment\n' '2:3: error: unterminated comment'
check_error location-comment-only $'/*' '1:1: error: unterminated comment'
check_error location-missing-rparen $'int main(void) {\n  return (1 + 2\n    ;\n}\n' '3:5: error: expect '\'')'\'''
check_error location-missing-rbracket $'int main(void) {\n  int a[2\n  ;\n}\n' '3:3: error: expect '\'']'\'''
check_error location-missing-colon $'int main(void) {\n  return 1 ? 2\n    ;\n}\n' '3:5: error: expect '\'':'\'''
check_error location-member-name $'struct S { int x; };\nint main(void) {\n  struct S s;\n  return s.\n    ;\n}\n' '5:5: error: expect member name'
check_error location-arrow-name $'struct S { int x; };\nint main(void) {\n  struct S *p;\n  return p->\n    ;\n}\n' '5:5: error: expect member name'
check_error location-function-eof $'int main(void) {\n  return 0;\n\n' '4:1: error: expect '\''}'\'''
check_error location-struct-eof $'struct S {\n  int x;\n\n' '4:1: error: expect '\''}'\'''
check_error location-empty-condition $'int main(void) {\n  if (\n    ) return 0;\n}\n' '3:5: error: expect expr'
check_error location-for-separator $'int main(void) {\n for (1) return 0;\n}\n' '2:8: error: expect '\'';'\'''
check_error location-call-comma $'int f(int x);\nint main(void) {\n  return f(1,\n    );\n}\n' '4:5: error: expect expr'
check_error location-subscript-expression $'int main(void) {\n  int a[2];\n  return a[\n    ];\n}\n' '4:5: error: expect expr'
check_error location-label-name $'int main(void) {\n  goto\n    ;\n}\n' '3:5: error: expect label name'
check_error location-unterminated-string $'int main(void) {\n  return "abc\n}\n' '2:10: error: unterminated string literal'
check_error location-unterminated-character $'int main(void) {\n  return \'a\n}\n' '2:10: error: unterminated character literal'

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
check_error missing-semicolon 'int main() { return 42 }' "1:23: error: expect ';'"
check_error extra-rparen 'int main() { return (1+2)); }' "1:26: error: expect ';'"
check_error adjacent-literals 'int main() { return 1 2; }' "1:22: error: expect ';'"
check_error trailing-identifier 'int main() { return 42; garbage }' "1:25: error: undeclared identifier"
check_error trailing-token 'int main() { return 42; @ }' "1:25: error: expect expr"
check_error trailing-after-bare-return 'int main() { return; garbage }' '1:14: error: expect return expr'
check_error empty-input '' "1:1: error: expect type specifier"
check_error whitespace-only $' \t\n' "2:1: error: expect type specifier"
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
check_error missing-decl-semicolon 'int main() { int a = 3 return a; }' "1:23: error: expect ';'"
check_error missing-decl-init 'int main() { int a =; return 0; }' '1:21: error: expect expr'
check_error nested-invalid 'int main() { int a; { int a; int a; } }' '1:34: error: duplicate local variable'
check_error bad-assignment-nested 'int main() { int a; a = 1 = 2; }' '1:27: error: invalid assignment target'
check_error missing-expr-semicolon 'int main() { int a; a = 3 return a; }' "1:26: error: expect ';'"
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
	     auto break case const continue default do double else extern \
	     float for goto if inline register restrict signed sizeof \
	     static switch union volatile while; do
    check_error "reserved-kw-$kw" \
		"int main() { int $kw; return 0; }" \
		'1:18: error: expect variable name'
done
for kw in enum struct void; do
    check_error "reserved-kw-$kw" \
                "int main() { int $kw; return 0; }" \
                '1:18: error: invalid combination of type specifiers'
done
check_error reserved-kw-typedef 'int main() { int typedef; return 0; }' \
            '1:25: error: expect typedef name'
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
	    "1:36: error: expect ';'"
check_error missing-do-body 'int main() { do }' \
	    '1:17: error: expect expr'
check_error unterminated-comment 'int main() { /* comment' \
	    '1:14: error: unterminated comment'
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
	    '1:40: error: not an integer constant expression'
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
	    "1:22: error: redefinition of parameter 'a'"
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
	    "1:29: error: expect ';'"
check_error goto-undefined 'int main() { goto L; }' \
	    "1:14: error: label 'L' not defined"
check_error duplicate-label 'int main() { L: ; L: ; }' \
	    '1:19: error: duplicate label'
check_error duplicate-label-scope 'int main() { { L: ; } L: ; }' \
	    '1:23: error: duplicate label'
check_error goto-missing-name 'int main() { goto 1; }' \
	    '1:19: error: expect label name'
check_error goto-missing-semi 'int main() { L: ; goto L }' \
	    "1:25: error: expect ';'"
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
