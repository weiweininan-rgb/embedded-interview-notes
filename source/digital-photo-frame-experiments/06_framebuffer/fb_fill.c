/*
 * fb_fill.c
 *
 * Fill /dev/fb0 with one color, then draw a small white block.
 *
 * Usage:
 *   ./fb_fill red
 *   ./fb_fill green
 *   ./fb_fill blue
 *   ./fb_fill black
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static int parse_color(const char *name, uint32_t *color)
{
    if (strcmp(name, "black") == 0) {
        *color = 0x00000000;
    } else if (strcmp(name, "red") == 0) {
        *color = 0x00ff0000;
    } else if (strcmp(name, "green") == 0) {
        *color = 0x0000ff00;
    } else if (strcmp(name, "blue") == 0) {
        *color = 0x000000ff;
    } else if (strcmp(name, "white") == 0) {
        *color = 0x00ffffff;
    } else {
        return -1;
    }

    return 0;
}

static void put_pixel(unsigned char *fb_mem,
                      unsigned int line_length,
                      unsigned int bytes_per_pixel,
                      unsigned int x,
                      unsigned int y,
                      uint32_t color)
{
    unsigned long offset;

    offset = (unsigned long)y * line_length +
             (unsigned long)x * bytes_per_pixel;

    *(uint32_t *)(fb_mem + offset) = color;
}

static void fill_screen(unsigned char *fb_mem,
                        const struct fb_var_screeninfo *var_info,
                        const struct fb_fix_screeninfo *fix_info,
                        uint32_t color)
{
    unsigned int bytes_per_pixel = var_info->bits_per_pixel / 8;
    unsigned int x;
    unsigned int y;

    for (y = 0; y < var_info->yres; y++) {
        for (x = 0; x < var_info->xres; x++) {
            put_pixel(fb_mem, fix_info->line_length, bytes_per_pixel, x, y, color);
        }
    }
}

static void draw_block(unsigned char *fb_mem,
                       const struct fb_var_screeninfo *var_info,
                       const struct fb_fix_screeninfo *fix_info,
                       uint32_t color)
{
    unsigned int bytes_per_pixel = var_info->bits_per_pixel / 8;
    unsigned int max_x = var_info->xres < 120 ? var_info->xres : 120;
    unsigned int max_y = var_info->yres < 120 ? var_info->yres : 120;
    unsigned int x;
    unsigned int y;

    for (y = 20; y < max_y; y++) {
        for (x = 20; x < max_x; x++) {
            put_pixel(fb_mem, fix_info->line_length, bytes_per_pixel, x, y, color);
        }
    }
}

int main(int argc, char *argv[])
{
    const char *color_name = "red";
    const char *fb_path = "/dev/fb0";
    struct fb_var_screeninfo var_info;
    struct fb_fix_screeninfo fix_info;
    unsigned char *fb_mem;
    unsigned long screen_size;
    uint32_t color;
    int fd;
    int result = 0;

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [black|red|green|blue|white]\n", argv[0]);
        return 1;
    }

    if (argc == 2) {
        color_name = argv[1];
    }

    if (parse_color(color_name, &color) == -1) {
        fprintf(stderr, "unknown color: %s\n", color_name);
        return 1;
    }

    fd = open(fb_path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "open %s: %s\n", fb_path, strerror(errno));
        return 1;
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &var_info) == -1) {
        perror("ioctl FBIOGET_VSCREENINFO");
        result = 1;
        goto out_close;
    }

    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix_info) == -1) {
        perror("ioctl FBIOGET_FSCREENINFO");
        result = 1;
        goto out_close;
    }

    if (var_info.bits_per_pixel != 32) {
        fprintf(stderr, "unsupported bits_per_pixel: %u\n", var_info.bits_per_pixel);
        result = 1;
        goto out_close;
    }

    screen_size = (unsigned long)fix_info.line_length *
                  (unsigned long)var_info.yres;

    fb_mem = mmap(NULL, screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb_mem == MAP_FAILED) {
        perror("mmap framebuffer");
        result = 1;
        goto out_close;
    }

    fill_screen(fb_mem, &var_info, &fix_info, color);
    draw_block(fb_mem, &var_info, &fix_info, 0x00ffffff);

    if (munmap(fb_mem, screen_size) == -1) {
        perror("munmap framebuffer");
        result = 1;
    }

out_close:
    if (close(fd) == -1) {
        perror("close framebuffer");
        result = 1;
    }

    return result;
}
