/*
 * fb_jpeg.c
 *
 * Decode a JPEG image with libjpeg and display it on /dev/fb0.
 *
 * Usage:
 *   ./fb_jpeg IMAGE.jpg
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

#include <jpeglib.h>

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
    const char *jpg_path;
    const char *fb_path = "/dev/fb0";
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    struct fb_var_screeninfo var_info;
    struct fb_fix_screeninfo fix_info;
    JSAMPARRAY row_buffer;
    unsigned char *fb_mem = MAP_FAILED;
    unsigned long screen_size = 0;
    unsigned int draw_width;
    unsigned int draw_height;
    unsigned int row_stride;
    FILE *jpg_fp = NULL;
    int fb_fd = -1;
    int jpeg_created = 0;
    int jpeg_started = 0;
    int result = 0;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s IMAGE.jpg\n", argv[0]);
        return 1;
    }

    jpg_path = argv[1];

    jpg_fp = fopen(jpg_path, "rb");
    if (jpg_fp == NULL) {
        fprintf(stderr, "open %s: %s\n", jpg_path, strerror(errno));
        return 1;
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

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_created = 1;

    jpeg_stdio_src(&cinfo, jpg_fp);
    jpeg_read_header(&cinfo, TRUE);

    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);
    jpeg_started = 1;

    if (cinfo.output_components != 3) {
        fprintf(stderr, "unsupported JPEG output components: %d\n", cinfo.output_components);
        result = 1;
        goto out_jpeg;
    }

    draw_width = cinfo.output_width < var_info.xres ?
                 cinfo.output_width : var_info.xres;
    draw_height = cinfo.output_height < var_info.yres ?
                  cinfo.output_height : var_info.yres;
    row_stride = cinfo.output_width * cinfo.output_components;

    row_buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo,
                                            JPOOL_IMAGE,
                                            row_stride,
                                            1);

    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned int y;
        unsigned int x;

        y = cinfo.output_scanline;
        jpeg_read_scanlines(&cinfo, row_buffer, 1);

        if (y >= draw_height) {
            continue;
        }

        for (x = 0; x < draw_width; x++) {
            unsigned char r;
            unsigned char g;
            unsigned char b;
            uint32_t color;
            unsigned int src_offset;

            src_offset = x * 3;
            r = row_buffer[0][src_offset + 0];
            g = row_buffer[0][src_offset + 1];
            b = row_buffer[0][src_offset + 2];
            color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;

            put_pixel32(fb_mem, fix_info.line_length, x, y, color);
        }
    }

out_jpeg:
    if (jpeg_started) {
        jpeg_finish_decompress(&cinfo);
    }
    if (jpeg_created) {
        jpeg_destroy_decompress(&cinfo);
    }

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
    if (fclose(jpg_fp) == EOF) {
        perror("close JPEG");
        result = 1;
    }

    return result;
}
