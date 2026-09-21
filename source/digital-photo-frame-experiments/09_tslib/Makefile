CC := gcc
CFLAGS := -Wall -Wextra -g -O0
TSLIB_CFLAGS ?=
TSLIB_LIBS ?= -lts

.PHONY: all clean

all: ts_read_simple

ts_read_simple: ts_read_simple.c
	$(CC) $(CFLAGS) $(TSLIB_CFLAGS) $< -o $@ $(TSLIB_LIBS)

clean:
	rm -f ts_read_simple
