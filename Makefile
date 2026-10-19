CC := gcc
OPT := -O2
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror $(OPT)
TARGET := cc

SRCS := main.c lexer.c parser.c codegen.c
OBJS := $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

test: $(TARGET)
	./test.sh

test-qemu:
	./test-qemu.sh

fmt:
	@echo "Formatting ..."
	@clang-format -i $(SRCS)

.PHONY: all test test-qemu clean fmt
