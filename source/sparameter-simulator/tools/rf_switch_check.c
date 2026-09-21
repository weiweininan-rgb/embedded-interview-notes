/* Smoke test for the future /dev/rf_switch UAPI; it only restores safe path. */
#include "rf_switch_uapi.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/dev/rf_switch";
    uint32_t current_path;
    uint32_t safe_path = RF_SWITCH_PATH_SAFE;
    int fd;

    fd = open(path, O_RDWR);
    if (fd == -1) {
        perror("open");
        return 1;
    }

    if (ioctl(fd, RF_SWITCH_GET_PATH, &current_path) == -1) {
        perror("GET_PATH");
        close(fd);
        return 1;
    }
    printf("GET_PATH=%u\n", current_path);

    /* This test never steps through calibration/DUT paths. */
    if (ioctl(fd, RF_SWITCH_SET_PATH, &safe_path) == -1) {
        perror("SET_PATH safe");
        close(fd);
        return 1;
    }
    printf("SET_PATH_SAFE=%u\n", safe_path);

    close(fd);
    return 0;
}
