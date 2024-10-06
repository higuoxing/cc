cc: cc.c vector.h
	clang cc.c -o cc

check: cc
	./test.sh
