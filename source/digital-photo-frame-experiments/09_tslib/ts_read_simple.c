/*
 * ts_read_simple.c
 *
 * Read touch samples with tslib.
 *
 * Usage:
 *   export TSLIB_TSDEVICE=/dev/input/event1
 *   ./ts_read_simple
 */

#include <stdio.h>
#include <stdlib.h>
#include <tslib.h>

int main(void)
{
    struct tsdev *ts;

    ts = ts_setup(NULL, 0);
    if (ts == NULL) {
        perror("ts_setup");
        return 1;
    }

    printf("reading tslib samples, press Ctrl+C to stop\n");

    while (1) {
        struct ts_sample sample;
        int ret;

        ret = ts_read(ts, &sample, 1);
        if (ret < 0) {
            perror("ts_read");
            ts_close(ts);
            return 1;
        }

        if (ret == 0) {
            continue;
        }

        printf("x=%d y=%d pressure=%u time=%ld.%06ld\n",
               sample.x,
               sample.y,
               sample.pressure,
               (long)sample.tv.tv_sec,
               (long)sample.tv.tv_usec);
        fflush(stdout);
    }

    ts_close(ts);
    return 0;
}
