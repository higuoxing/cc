CC := gcc
CFLAGS := -Wall -Wextra -O2
TARGET := cc

SRCS := $(wildcard *.c)
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

fmt:
	@echo "Formatting ..."
	@clang-format -i $(SRCS)

.PHONY: all test clean fmt
