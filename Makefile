cc: cc.c
	clang cc.c -o cc

check: cc
	./test.sh
