CC=gcc
CFLAGS=-O3 -Wall -Wextra -Wpedantic -std=c99 -flto

all: bin/index bin/start bin/move bin/end

bin/index: index.c | bin/
	$(CC) $(CFLAGS) $^ -o $@

bin/start: start.c | bin/
	$(CC) $(CFLAGS) $^ -o $@

bin/move: move.c jsonw.c jsonw.h tictac.h | bin/
	$(CC) $(CFLAGS) -march=native -Wno-parentheses -Wno-unused-value \
		move.c jsonw.c -o $@

bin/end: end.c | bin/
	$(CC) $(CFLAGS) $^ -o $@

bin/:
	mkdir bin/

clean:
	rm -rf bin/
