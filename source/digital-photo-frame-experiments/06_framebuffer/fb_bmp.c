/*
 * fb_bmp.c
 *
 * Display an uncompressed 24-bit or 32-bit BMP on /dev/fb0.
 *
 * Usage:
 *   ./fb_bmp IMAGE.bmp
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

struct bmp_info {
    uint32_t data_offset;
    int32_t width;
    int32_t height;
    uint16_t bits_per_pixel;
    uint32_t compression;
};

static uint16_t read_le16(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const unsigned char *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int32_t read_le32s(const unsigned char *p)
{
    return (int32_t)read_le32(p);
}

static int read_bmp_info(FILE *fp, struct bmp_info *info)
{
    unsigned char header[54];

    if (fread(header, 1, sizeof(header), fp) != sizeof(header)) {
        fprintf(stderr, "failed to read BMP header\n");
        return -1;
    }

    if (header[0] != 'B' || header[1] != 'M') {
        fprintf(stderr, "not a BMP file\n");
        return -1;
    }

    info->data_offset = read_le32(header + 10);
    info->width = read_le32s(header + 18);
    info->height = read_le32s(header + 22);
    info->bits_per_pixel = read_le16(header + 28);
    info->compression = read_le32(header + 30);

    if (info->width <= 0 || info->height == 0) {
        fprintf(stderr, "unsupported BMP size: %d x %d\n", info->width, info->height);
        return -1;
    }

    if (info->bits_per_pixel != 24 && info->bits_per_pixel != 32) {
        fprintf(stderr, "unsupported BMP bpp: %u\n", info->bits_per_pixel);
        return -1;
    }

    if (info->compression != 0) {
        fprintf(stderr, "compressed BMP is not supported\n");
        return -1;
    }

    return 0;
}

static unsigned int abs_height(int32_t height)
{
    if (height < 0) {
        return (unsigned int)(-height);
    }

    return (unsigned int)height;
}

static void put_pixel32(unsigned char *fb_mem,
                        unsigned int line_length,
                        unsigned int x,
                        unsigned int y,
                        uint32_t color)
{
    unsigned long offset;

    offset = (unsigned long)y * line_length + (unsigned long)x * 4;
    *(uint32_t *)(fb_mem + offset) = color;
}

int main(int argc, char *argv[])
{
    const char *bmp_path;
    const char *fb_path = "/dev/fb0";
    struct bmp_info bmp;
    struct fb_var_screeninfo var_info;
    struct fb_fix_screeninfo fix_info;
    unsigned char *fb_mem;
    unsigned char *row_buf = NULL;
    unsigned long screen_size;
    unsigned int bmp_height;
    unsigned int draw_width;
    unsigned int draw_height;
    unsigned int row_size;
    unsigned int src_y;
    FILE *bmp_fp = NULL;
    int fb_fd = -1;
    int result = 0;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s IMAGE.bmp\n", argv[0]);
        return 1;
    }

    bmp_path = argv[1];

    bmp_fp = fopen(bmp_path, "rb");
    if (bmp_fp == NULL) {
        fprintf(stderr, "open %s: %s\n", bmp_path, strerror(errno));
        return 1;
    }

    if (read_bmp_info(bmp_fp, &bmp) == -1) {
        result = 1;
        goto out_file;
    }

    fb_fd = open(fb_path, O_RDWR);
    if (fb_fd == -1) {
        fprintf(stderr, "open %s: %s\n", fb_path, strerror(errno));
        result = 1;
        goto out_file;
    }

    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &var_info) == -1) {
        perror("ioctl FBIOGET_VSCREENINFO");
        result = 1;
        goto out_fb;
    }

    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix_info) == -1) {
        perror("ioctl FBIOGET_FSCREENINFO");
        result = 1;
        goto out_fb;
    }

    if (var_info.bits_per_pixel != 32) {
        fprintf(stderr, "unsupported framebuffer bpp: %u\n", var_info.bits_per_pixel);
        result = 1;
        goto out_fb;
    }

    screen_size = (unsigned long)fix_info.line_length *
                  (unsigned long)var_info.yres;

    fb_mem = mmap(NULL, screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) {
        perror("mmap framebuffer");
        result = 1;
        goto out_fb;
    }

    bmp_height = abs_height(bmp.height);
    draw_width = (unsigned int)bmp.width < var_info.xres ?
                 (unsigned int)bmp.width : var_info.xres;
    draw_height = bmp_height < var_info.yres ? bmp_height : var_info.yres;
    row_size = (((unsigned int)bmp.width * bmp.bits_per_pixel + 31) / 32) * 4;

    row_buf = malloc(row_size);
    if (row_buf == NULL) {
        perror("malloc row buffer");
        result = 1;
        goto out_map;
    }

    for (src_y = 0; src_y < draw_height; src_y++) {
        unsigned int screen_y;
        unsigned int x;
        long row_offset;

        if (bmp.height > 0) {
            screen_y = draw_height - 1 - src_y;
        } else {
            screen_y = src_y;
        }

        row_offset = (long)bmp.data_offset + (long)src_y * (long)row_size;
        if (fseek(bmp_fp, row_offset, SEEK_SET) == -1) {
            perror("fseek BMP row");
            result = 1;
            break;
        }

        if (fread(row_buf, 1, row_size, bmp_fp) != row_size) {
            fprintf(stderr, "failed to read BMP row\n");
            result = 1;
            break;
        }

        for (x = 0; x < draw_width; x++) {
            unsigned int src_offset;
            unsigned char b;
            unsigned char g;
            unsigned char r;
            uint32_t color;

            src_offset = x * (bmp.bits_per_pixel / 8);
            b = row_buf[src_offset + 0];
            g = row_buf[src_offset + 1];
            r = row_buf[src_offset + 2];
            color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;

            put_pixel32(fb_mem, fix_info.line_length, x, screen_y, color);
        }
    }

    free(row_buf);

out_map:
    if (munmap(fb_mem, screen_size) == -1) {
        perror("munmap framebuffer");
        result = 1;
    }

out_fb:
    if (fb_fd != -1 && close(fb_fd) == -1) {
        perror("close framebuffer");
        result = 1;
    }

out_file:
    if (fclose(bmp_fp) == EOF) {
        perror("close BMP");
        result = 1;
    }

    return result;
}
