CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -g
TARGET := hello
SRC := hello.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGET)
