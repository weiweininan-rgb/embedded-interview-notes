CC := gcc
CFLAGS := -Wall -Wextra -g -O0
FREETYPE_CFLAGS ?=
FREETYPE_LIBS ?= -lfreetype -lm

.PHONY: all clean

all: ft_char

ft_char: ft_char.c
	$(CC) $(CFLAGS) $(FREETYPE_CFLAGS) $< -o $@ $(FREETYPE_LIBS)

clean:
	rm -f ft_char
