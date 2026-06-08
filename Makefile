.POSIX:
.SUFFIXES:
CC=gcc
CFLAGS=-O3 -Wall -Wextra -Wpedantic -std=c99 -march=native -flto

all: bin/move bin/index bin/start bin/end
bin/:; mkdir bin/
clean:; rm -rf bin/

bin/move:  bin/ vendor/jsonw.h vendor/jsonw.c move.c;  $(CC) $(CFLAGS) -o $@ vendor/jsonw.c move.c  -Wno-sign-compare -Wno-parentheses -Wno-unused-value -Wno-missing-field-initializers
bin/index: bin/ vendor/jsonw.h vendor/jsonw.c index.c; $(CC) $(CFLAGS) -o $@ vendor/jsonw.c index.c -Wno-sign-compare -Wno-parentheses -Wno-unused-value
bin/start: bin/ start.c; $(CC) $(CFLAGS) -o $@ start.c
bin/end:   bin/ end.c;   $(CC) $(CFLAGS) -o $@ end.c
