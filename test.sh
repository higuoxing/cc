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

diff -u <(./cc --debug-dump-tokens '[ ] ( ) { } . -> ++  --  & * + - ~ ! / % << >> < > <= >= == != ^ | && || ? : ; ... = *= /= %= += -= <<= >>= &= ^= |= , # ## <: :> <% %> %: %:%:') <(cat <<EOF
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
TK_EOF line: -1 column: -1
EOF
)

diff -u <(./cc --debug-dump-tokens ' [ ] ( ) 123 + 456 ') <(cat <<EOF
TK_PUNCTUATOR '[' line: 1 column: 1
TK_PUNCTUATOR ']' line: 1 column: 3
TK_PUNCTUATOR '(' line: 1 column: 5
TK_PUNCTUATOR ')' line: 1 column: 7
TK_CONSTANT '123' line: 1 column: 9
TK_PUNCTUATOR '+' line: 1 column: 13
TK_CONSTANT '456' line: 1 column: 15
TK_EOF line: -1 column: -1
EOF
)
# check 0 0
# check 42 42
# check 1 '1+0'
# check 42 '11+22+9'
# check 42 '11  +  22 +    9'
