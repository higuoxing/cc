SOURCES = $(wildcard *.c)
HEADERS = $(wildcard *.h)
cc: $(SOURCES) $(HEADERS)
	clang $(SOURCES) -o cc

check: cc
	./test.sh

.PHONY: clean
clean:
	rm -f cc
