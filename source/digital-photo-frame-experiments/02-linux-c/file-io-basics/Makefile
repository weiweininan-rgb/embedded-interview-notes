CC := gcc
CFLAGS := -Wall -Wextra -g -O0
TARGET := file_copy
SRC := file_copy.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGET)
