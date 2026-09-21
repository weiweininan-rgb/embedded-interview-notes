/*
 * ft_char.c
 *
 * Render one Unicode codepoint with FreeType and draw it on /dev/fb0.
 *
 * Usage:
 *   ./ft_char FONT CODEPOINT [X Y SIZE]
 * Example:
 *   ./ft_char MSYH.TTF 0x41 100 150 72
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

#include <ft2build.h>
#include FT_FREETYPE_H

static uint32_t blend_white(uint32_t old_color, unsigned char alpha)
{
    unsigned int r;
    unsigned int g;
    unsigned int b;

    r = (old_color >> 16) & 0xff;
    g = (old_color >> 8) & 0xff;
    b = old_color & 0xff;

    r = r + ((255 - r) * alpha) / 255;
    g = g + ((255 - g) * alpha) / 255;
    b = b + ((255 - b) * alpha) / 255;

    return (r << 16) | (g << 8) | b;
}

static void draw_gray_bitmap(unsigned char *fb_mem,
                             const struct fb_var_screeninfo *var_info,
                             const struct fb_fix_screeninfo *fix_info,
                             const FT_Bitmap *bitmap,
                             int x0,
                             int y0)
{
    unsigned int row;
    unsigned int col;

    for (row = 0; row < bitmap->rows; row++) {
        int y = y0 + (int)row;

        if (y < 0 || y >= (int)var_info->yres) {
            continue;
        }

        for (col = 0; col < bitmap->width; col++) {
            int x = x0 + (int)col;
            unsigned char alpha;
            unsigned long offset;
            uint32_t *pixel;

            if (x < 0 || x >= (int)var_info->xres) {
                continue;
            }

            alpha = bitmap->buffer[row * bitmap->pitch + col];
            if (alpha == 0) {
                continue;
            }

            offset = (unsigned long)y * fix_info->line_length +
                     (unsigned long)x * 4;
            pixel = (uint32_t *)(fb_mem + offset);
            *pixel = blend_white(*pixel, alpha);
        }
    }
}

static unsigned long parse_ulong(const char *text, const char *name)
{
    char *endptr;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &endptr, 0);
    if (errno != 0 || *endptr != '\0') {
        fprintf(stderr, "invalid %s: %s\n", name, text);
        exit(1);
    }

    return value;
}

int main(int argc, char *argv[])
{
    const char *font_path;
    unsigned long codepoint;
    int pen_x = 100;
    int baseline_y = 150;
    unsigned int pixel_size = 72;
    int fb_fd = -1;
    unsigned char *fb_mem = MAP_FAILED;
    unsigned long screen_size;
    struct fb_var_screeninfo var_info;
    struct fb_fix_screeninfo fix_info;
    FT_Library library = NULL;
    FT_Face face = NULL;
    FT_GlyphSlot glyph;
    int result = 0;

    if (argc != 3 && argc != 6) {
        fprintf(stderr, "Usage: %s FONT CODEPOINT [X Y SIZE]\n", argv[0]);
        return 1;
    }

    font_path = argv[1];
    codepoint = parse_ulong(argv[2], "codepoint");

    if (argc == 6) {
        pen_x = (int)parse_ulong(argv[3], "x");
        baseline_y = (int)parse_ulong(argv[4], "y");
        pixel_size = (unsigned int)parse_ulong(argv[5], "size");
    }

    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd == -1) {
        perror("open /dev/fb0");
        return 1;
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

    if (FT_Init_FreeType(&library) != 0) {
        fprintf(stderr, "FT_Init_FreeType failed\n");
        result = 1;
        goto out_mmap;
    }

    if (FT_New_Face(library, font_path, 0, &face) != 0) {
        fprintf(stderr, "FT_New_Face failed: %s\n", font_path);
        result = 1;
        goto out_ft;
    }

    if (FT_Set_Pixel_Sizes(face, 0, pixel_size) != 0) {
        fprintf(stderr, "FT_Set_Pixel_Sizes failed\n");
        result = 1;
        goto out_face;
    }

    if (FT_Load_Char(face, codepoint, FT_LOAD_RENDER) != 0) {
        fprintf(stderr, "FT_Load_Char failed: U+%lX\n", codepoint);
        result = 1;
        goto out_face;
    }

    glyph = face->glyph;
    draw_gray_bitmap(fb_mem,
                     &var_info,
                     &fix_info,
                     &glyph->bitmap,
                     pen_x + glyph->bitmap_left,
                     baseline_y - glyph->bitmap_top);

out_face:
    if (face != NULL) {
        FT_Done_Face(face);
    }
out_ft:
    if (library != NULL) {
        FT_Done_FreeType(library);
    }
out_mmap:
    if (munmap(fb_mem, screen_size) == -1) {
        perror("munmap framebuffer");
        result = 1;
    }
out_fb:
    if (close(fb_fd) == -1) {
        perror("close framebuffer");
        result = 1;
    }

    return result;
}
