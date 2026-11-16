CC = gcc
# Use -O3 for the compiler itself too; -O2 for debuggable dev builds.
# Override with `make dev` or `make CFLAGS=-O0 -g`.
CFLAGS = -I src -std=c11 -Wall -Wextra -O3 -flto -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lm -lpthread

SRCS = $(wildcard src/*.c) $(wildcard src/codegen/*.c)
OBJS = $(SRCS:.c=.o)
TARGET = kodas

.PHONY: all clean test dev

all: $(TARGET)

dev: CFLAGS := -I src -std=c11 -Wall -Wextra -O0 -g -D_POSIX_C_SOURCE=200809L -DKODAS_DEBUG
dev: clean $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@strip $(TARGET) 2>/dev/null || true
	@echo "Built $(TARGET) (`du -h $(TARGET) | cut -f1`)"

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

test: $(TARGET)
	@echo "=== S17 smoke tests ==="
	@echo 'fn fib(n){if n<2{give n} give fib(n-1)+fib(n-2)} show fib(35)' > /tmp/fib.kds
	@./$(TARGET) run /tmp/fib.kds
	@echo 'show "hello"' | ./$(TARGET) run /dev/stdin
