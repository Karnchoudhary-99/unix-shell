CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -g
SRC = src/main.c
BIN = ush

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

test: $(BIN)
	./tests/smoke.sh

clean:
	rm -f $(BIN)

.PHONY: all test clean
