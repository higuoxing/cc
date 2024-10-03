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

check 0 0
check 42 42
check 1 '1+0'
check 42 '11+22+9'
check 42 '11  +  22 +    9'
