CC := gcc
OPT := -O2
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror $(OPT)
TARGET := cc

SRCS := main.c lexer.c type.c parser.c sema.c layout.c codegen.c vector.c
OBJS := $(SRCS:.c=.o)
DEPS := $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)

test: $(TARGET)
	./test.sh

fmt:
	@echo "Formatting ..."
	@clang-format -i $(SRCS)

.PHONY: all test clean fmt
