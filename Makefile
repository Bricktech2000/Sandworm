CC=gcc
CFLAGS=-O3 -Wall -Wextra -Wpedantic -std=c99 -flto

all: bin/index bin/start bin/move bin/end

bin/index: index.c vendor/jsonw.c vendor/jsonw.h | bin/
	$(CC) $(CFLAGS) -Wno-sign-compare -Wno-parentheses -Wno-unused-value \
		index.c vendor/jsonw.c -o $@

bin/start: start.c | bin/
	$(CC) $(CFLAGS) $^ -o $@

bin/move: move.c vendor/jsonw.c vendor/jsonw.h | bin/
	$(CC) $(CFLAGS) -march=native -Wno-sign-compare -Wno-parentheses -Wno-unused-value -Wno-missing-field-initializers \
		move.c vendor/jsonw.c -o $@

bin/end: end.c | bin/
	$(CC) $(CFLAGS) $^ -o $@

bin/:
	mkdir bin/

clean:
	rm -rf bin/
