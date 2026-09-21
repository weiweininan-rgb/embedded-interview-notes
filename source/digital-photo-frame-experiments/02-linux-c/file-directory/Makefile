CC := gcc
CFLAGS := -Wall -Wextra -g -O0
TARGET := mini_browser
SRC := mini_browser.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGET)
