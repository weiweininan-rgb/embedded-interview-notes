/*
 * input_event_dump.c
 *
 * Read Linux input events from /dev/input/eventX and print type/code/value.
 *
 * Usage:
 *   ./input_event_dump /dev/input/event1
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *type_name(unsigned short type)
{
    switch (type) {
    case EV_SYN:
        return "EV_SYN";
    case EV_KEY:
        return "EV_KEY";
    case EV_ABS:
        return "EV_ABS";
    case EV_REL:
        return "EV_REL";
    default:
        return "EV_OTHER";
    }
}

static const char *code_name(unsigned short type, unsigned short code)
{
    if (type == EV_SYN && code == SYN_REPORT) {
        return "SYN_REPORT";
    }

    if (type == EV_KEY && code == BTN_TOUCH) {
        return "BTN_TOUCH";
    }

    if (type == EV_ABS) {
        switch (code) {
        case ABS_X:
            return "ABS_X";
        case ABS_Y:
            return "ABS_Y";
        case ABS_MT_POSITION_X:
            return "ABS_MT_POSITION_X";
        case ABS_MT_POSITION_Y:
            return "ABS_MT_POSITION_Y";
        case ABS_MT_TRACKING_ID:
            return "ABS_MT_TRACKING_ID";
        default:
            return "ABS_OTHER";
        }
    }

    return "CODE_OTHER";
}

int main(int argc, char *argv[])
{
    const char *event_path;
    int fd;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s /dev/input/eventX\n", argv[0]);
        return 1;
    }

    event_path = argv[1];
    fd = open(event_path, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "open %s: %s\n", event_path, strerror(errno));
        return 1;
    }

    printf("reading %s, press Ctrl+C to stop\n", event_path);

    while (1) {
        struct input_event ev;
        ssize_t nread;

        nread = read(fd, &ev, sizeof(ev));
        if (nread == -1) {
            perror("read input_event");
            close(fd);
            return 1;
        }

        if (nread != sizeof(ev)) {
            fprintf(stderr, "short read: %zd bytes\n", nread);
            close(fd);
            return 1;
        }

        printf("%ld.%06ld  %-8s type=%u  %-18s code=%u  value=%d\n",
               (long)ev.time.tv_sec,
               (long)ev.time.tv_usec,
               type_name(ev.type),
               ev.type,
               code_name(ev.type, ev.code),
               ev.code,
               ev.value);
        fflush(stdout);
    }

    close(fd);
    return 0;
}
