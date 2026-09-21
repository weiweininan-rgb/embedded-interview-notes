CC := gcc
CFLAGS := -Wall -Wextra -g -O0
TARGETS := fb_info fb_fill fb_bmp fb_jpeg
JPEG_CFLAGS ?=
JPEG_LIBS ?= -ljpeg

.PHONY: all clean

all: $(TARGETS)

fb_info: fb_info.c
	$(CC) $(CFLAGS) $< -o $@

fb_fill: fb_fill.c
	$(CC) $(CFLAGS) $< -o $@

fb_bmp: fb_bmp.c
	$(CC) $(CFLAGS) $< -o $@

fb_jpeg: fb_jpeg.c
	$(CC) $(CFLAGS) $(JPEG_CFLAGS) $< -o $@ $(JPEG_LIBS)

clean:
	rm -f $(TARGETS)
