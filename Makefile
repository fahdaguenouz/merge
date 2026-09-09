CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2
CPPFLAGS ?= -D_GNU_SOURCE

.PHONY: all clean test

all: merge bin1 bin2

merge: src/merge.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

bin1: tests/bin1.c
	$(CC) $(CFLAGS) $< -o $@

bin2: tests/bin2.c
	$(CC) $(CFLAGS) $< -o $@

test: all
	sh tests/test_merge.sh

clean:
	rm -f merge bin1 bin2 bin3
