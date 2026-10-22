CC = gcc
CFLAGS = -I src -std=c11 -Wall -Wextra -O2 -g -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lm

SRCS = $(wildcard src/*.c) $(wildcard src/codegen/*.c)
OBJS = $(SRCS:.c=.o)
TARGET = kodas

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

test: $(TARGET)
	@echo "Running tests..."
	@./$(TARGET) run ../tests/hello.kds
	@./$(TARGET) run ../tests/math.kds
	@./$(TARGET) run ../tests/features.kds
