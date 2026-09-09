CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2
CPPFLAGS ?= -D_GNU_SOURCE -Iinclude

MERGE_SOURCES := src/main.c src/io.c src/format.c src/binder.c src/runner.c

.PHONY: all clean test

all: merge bin1 bin2

merge: $(MERGE_SOURCES) include/merge.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(MERGE_SOURCES) -o $@

bin1: tests/bin1.c
	$(CC) $(CFLAGS) $< -o $@

bin2: tests/bin2.c
	$(CC) $(CFLAGS) $< -o $@

test: all
	sh tests/test_merge.sh

clean:
	rm -f merge bin1 bin2 bin3
