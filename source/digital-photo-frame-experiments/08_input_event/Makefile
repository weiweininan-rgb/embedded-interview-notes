CC := gcc
CFLAGS := -Wall -Wextra -g -O0

.PHONY: all clean

all: input_event_dump

input_event_dump: input_event_dump.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f input_event_dump
