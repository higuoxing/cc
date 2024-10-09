#!/usr/bin/env bash

mkdir -p ./tmp/

function check() {
    expected="$1";
    input="$2";

    ./cc "$input" > ./tmp/tmp.s || exit 1
    gcc -static -o ./tmp/tmp ./tmp/tmp.s
    ./tmp/tmp
    actual="$?"

    if [[ "$expected" == "$actual" ]]; then
	echo "$input => $actual"
    else
	echo "$input => $expected expected, but got $actual"
	exit 1
    fi
}

diff -u <(./cc --debug-only-tokenize --debug-dump-tokens '[ ] ( ) { } . -> ++  --  & * + - ~ ! / % << >> < > <= >= == != ^ | && || ? : ; ... = *= /= %= += -= <<= >>= &= ^= |= , # ## <: :> <% %> %: %:%: auto if unsigned break inline void case int volatile char long while const register _Alignas continue restrict _Alignof default return _Atomic do short _Bool double signed _Complex else sizeof _Generic enum static _Imaginary extern struct _Noreturn float switch _Static_assert for typedef _Thread_local goto union') <(cat <<EOF
TK_PUNCTUATOR '[' line: 1 column: 0
TK_PUNCTUATOR ']' line: 1 column: 2
TK_PUNCTUATOR '(' line: 1 column: 4
TK_PUNCTUATOR ')' line: 1 column: 6
TK_PUNCTUATOR '{' line: 1 column: 8
TK_PUNCTUATOR '}' line: 1 column: 10
TK_PUNCTUATOR '.' line: 1 column: 12
TK_PUNCTUATOR '->' line: 1 column: 14
TK_PUNCTUATOR '++' line: 1 column: 17
TK_PUNCTUATOR '--' line: 1 column: 21
TK_PUNCTUATOR '&' line: 1 column: 25
TK_PUNCTUATOR '*' line: 1 column: 27
TK_PUNCTUATOR '+' line: 1 column: 29
TK_PUNCTUATOR '-' line: 1 column: 31
TK_PUNCTUATOR '~' line: 1 column: 33
TK_PUNCTUATOR '!' line: 1 column: 35
TK_PUNCTUATOR '/' line: 1 column: 37
TK_PUNCTUATOR '%' line: 1 column: 39
TK_PUNCTUATOR '<<' line: 1 column: 41
TK_PUNCTUATOR '>>' line: 1 column: 44
TK_PUNCTUATOR '<' line: 1 column: 47
TK_PUNCTUATOR '>' line: 1 column: 49
TK_PUNCTUATOR '<=' line: 1 column: 51
TK_PUNCTUATOR '>=' line: 1 column: 54
TK_PUNCTUATOR '==' line: 1 column: 57
TK_PUNCTUATOR '!=' line: 1 column: 60
TK_PUNCTUATOR '^' line: 1 column: 63
TK_PUNCTUATOR '|' line: 1 column: 65
TK_PUNCTUATOR '&&' line: 1 column: 67
TK_PUNCTUATOR '||' line: 1 column: 70
TK_PUNCTUATOR '?' line: 1 column: 73
TK_PUNCTUATOR ':' line: 1 column: 75
TK_PUNCTUATOR ';' line: 1 column: 77
TK_PUNCTUATOR '...' line: 1 column: 79
TK_PUNCTUATOR '=' line: 1 column: 83
TK_PUNCTUATOR '*=' line: 1 column: 85
TK_PUNCTUATOR '/=' line: 1 column: 88
TK_PUNCTUATOR '%=' line: 1 column: 91
TK_PUNCTUATOR '+=' line: 1 column: 94
TK_PUNCTUATOR '-=' line: 1 column: 97
TK_PUNCTUATOR '<<=' line: 1 column: 100
TK_PUNCTUATOR '>>=' line: 1 column: 104
TK_PUNCTUATOR '&=' line: 1 column: 108
TK_PUNCTUATOR '^=' line: 1 column: 111
TK_PUNCTUATOR '|=' line: 1 column: 114
TK_PUNCTUATOR ',' line: 1 column: 117
TK_PUNCTUATOR '#' line: 1 column: 119
TK_PUNCTUATOR '##' line: 1 column: 121
TK_PUNCTUATOR '<:' line: 1 column: 124
TK_PUNCTUATOR ':>' line: 1 column: 127
TK_PUNCTUATOR '<%' line: 1 column: 130
TK_PUNCTUATOR '%>' line: 1 column: 133
TK_PUNCTUATOR '%:' line: 1 column: 136
TK_PUNCTUATOR '%:%:' line: 1 column: 139
TK_PUNCTUATOR 'auto' line: 1 column: 144
TK_PUNCTUATOR 'if' line: 1 column: 149
TK_PUNCTUATOR 'unsigned' line: 1 column: 152
TK_PUNCTUATOR 'break' line: 1 column: 161
TK_PUNCTUATOR 'inline' line: 1 column: 167
TK_PUNCTUATOR 'void' line: 1 column: 174
TK_PUNCTUATOR 'case' line: 1 column: 179
TK_PUNCTUATOR 'int' line: 1 column: 184
TK_PUNCTUATOR 'volatile' line: 1 column: 188
TK_PUNCTUATOR 'char' line: 1 column: 197
TK_PUNCTUATOR 'long' line: 1 column: 202
TK_PUNCTUATOR 'while' line: 1 column: 207
TK_PUNCTUATOR 'const' line: 1 column: 213
TK_PUNCTUATOR 'register' line: 1 column: 219
TK_PUNCTUATOR '_Alignas' line: 1 column: 228
TK_PUNCTUATOR 'continue' line: 1 column: 237
TK_PUNCTUATOR 'restrict' line: 1 column: 246
TK_PUNCTUATOR '_Alignof' line: 1 column: 255
TK_PUNCTUATOR 'default' line: 1 column: 264
TK_PUNCTUATOR 'return' line: 1 column: 272
TK_PUNCTUATOR '_Atomic' line: 1 column: 279
TK_PUNCTUATOR 'do' line: 1 column: 287
TK_PUNCTUATOR 'short' line: 1 column: 290
TK_PUNCTUATOR '_Bool' line: 1 column: 296
TK_PUNCTUATOR 'double' line: 1 column: 302
TK_PUNCTUATOR 'signed' line: 1 column: 309
TK_PUNCTUATOR '_Complex' line: 1 column: 316
TK_PUNCTUATOR 'else' line: 1 column: 325
TK_PUNCTUATOR 'sizeof' line: 1 column: 330
TK_PUNCTUATOR '_Generic' line: 1 column: 337
TK_PUNCTUATOR 'enum' line: 1 column: 346
TK_PUNCTUATOR 'static' line: 1 column: 351
TK_PUNCTUATOR '_Imaginary' line: 1 column: 358
TK_PUNCTUATOR 'extern' line: 1 column: 369
TK_PUNCTUATOR 'struct' line: 1 column: 376
TK_PUNCTUATOR '_Noreturn' line: 1 column: 383
TK_PUNCTUATOR 'float' line: 1 column: 393
TK_PUNCTUATOR 'switch' line: 1 column: 399
TK_PUNCTUATOR '_Static_assert' line: 1 column: 406
TK_PUNCTUATOR 'for' line: 1 column: 421
TK_PUNCTUATOR 'typedef' line: 1 column: 425
TK_PUNCTUATOR '_Thread_local' line: 1 column: 433
TK_PUNCTUATOR 'goto' line: 1 column: 447
TK_PUNCTUATOR 'union' line: 1 column: 452
TK_EOF line: -1 column: -1
EOF
)

diff -u <(./cc --debug-dump-ir --debug-only-dump-ir 'return') <(cat <<EOF
start:
  ret
EOF
)
