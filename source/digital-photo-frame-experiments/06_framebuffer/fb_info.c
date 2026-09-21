/*
 * fb_info.c
 *
 * Print basic framebuffer parameters from /dev/fb0.
 *
 * Usage:
 *   ./fb_info
 *   ./fb_info /dev/fb1
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    const char *fb_path = "/dev/fb0";
    struct fb_var_screeninfo var_info;
    struct fb_fix_screeninfo fix_info;
    unsigned long visible_bytes;
    unsigned long stride_bytes;
    int fd;
    int result = 0;

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [FRAMEBUFFER]\n", argv[0]);
        return 1;
    }

    if (argc == 2) {
        fb_path = argv[1];
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

    visible_bytes = (unsigned long)var_info.xres *
                    (unsigned long)var_info.yres *
                    (unsigned long)var_info.bits_per_pixel / 8;
    stride_bytes = (unsigned long)fix_info.line_length *
                   (unsigned long)var_info.yres;

    printf("framebuffer: %s\n", fb_path);
    printf("xres: %u\n", var_info.xres);
    printf("yres: %u\n", var_info.yres);
    printf("bits_per_pixel: %u\n", var_info.bits_per_pixel);
    printf("line_length: %u\n", fix_info.line_length);
    printf("smem_len: %u\n", fix_info.smem_len);
    printf("visible_bytes: %lu\n", visible_bytes);
    printf("line_length * yres: %lu\n", stride_bytes);

out_close:
    if (close(fd) == -1) {
        perror("close framebuffer");
        result = 1;
    }

    return result;
}
