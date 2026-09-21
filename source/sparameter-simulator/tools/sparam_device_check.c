#define _POSIX_C_SOURCE 200809L

/* 板端驱动 ABI、阻塞 read 语义和 poll 就绪状态的冒烟测试。 */

#include "sparam_sim_uapi.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static int elapsed_ms(const struct timespec *start, const struct timespec *end)
{
    /* 单调时钟使阻塞时长不受系统校时影响。 */
    return (int)((end->tv_sec - start->tv_sec) * 1000L +
                 (end->tv_nsec - start->tv_nsec) / 1000000L);
}

static int get_status(int fd, unsigned int *status)
{
    /* 在每个状态转换检查点统一输出 ioctl 结果。 */
    if (ioctl(fd, SPARAM_SIM_GET_STATUS, status) == -1) {
        perror("GET_STATUS");
        return -1;
    }
    printf("GET_STATUS=%u\n", *status);
    return 0;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/dev/sparam_sim";
    struct sparam_sim_frame frame;
    struct pollfd descriptor;
    struct timespec start;
    struct timespec end;
    unsigned int status;
    unsigned int rate_ms = 100;
    unsigned int invalid_rate_ms = SPARAM_SIM_RATE_MIN_MS - 1U;
    ssize_t count;
    int fd;
    int poll_result;
    int result = 1;

    fd = open(path, O_RDONLY);
    if (fd == -1) {
        perror("open");
        return 1;
    }
    printf("OPEN=%s\n", path);

    if (get_status(fd, &status) == -1) goto out;

    /* 先证明非法速率会被拒绝，再配置有效测试间隔。 */
    errno = 0;
    if (ioctl(fd, SPARAM_SIM_SET_RATE, &invalid_rate_ms) != -1 || errno != EINVAL) {
        fprintf(stderr, "SET_RATE invalid: expected EINVAL, got errno=%d\n", errno);
        goto out;
    }
    printf("SET_RATE invalid=%u -> EINVAL\n", invalid_rate_ms);

    if (ioctl(fd, SPARAM_SIM_SET_RATE, &rate_ms) == -1) {
        perror("SET_RATE");
        goto out;
    }
    printf("SET_RATE=%u ms\n", rate_ms);

    if (ioctl(fd, SPARAM_SIM_START) == -1) {
        perror("START");
        goto out;
    }
    printf("START=ok\n");

    if (clock_gettime(CLOCK_MONOTONIC, &start) == -1) {
        perror("clock_gettime");
        goto stop;
    }
    /* read() 应阻塞，直到延迟工作发布首个完整帧。 */
    count = read(fd, &frame, sizeof(frame));
    if (clock_gettime(CLOCK_MONOTONIC, &end) == -1) {
        perror("clock_gettime");
        goto stop;
    }
    if (count != (ssize_t)sizeof(frame)) {
        fprintf(stderr, "blocking read: expected %zu bytes, got %zd (errno=%d)\n",
                sizeof(frame), count, errno);
        goto stop;
    }
    if (frame.sample_count != SPARAM_SIM_SAMPLES) {
        fprintf(stderr, "blocking read: expected %u samples, got %u\n",
                SPARAM_SIM_SAMPLES, frame.sample_count);
        goto stop;
    }
    printf("READ_BLOCKING=%zd bytes elapsed=%d ms sequence=%" PRIu64 "\n",
           count, elapsed_ms(&start, &end), (uint64_t)frame.sequence);

    descriptor.fd = fd;
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    if (clock_gettime(CLOCK_MONOTONIC, &start) == -1) {
        perror("clock_gettime");
        goto stop;
    }
    /* 下一帧必须先通过 poll() 表现为可读。 */
    poll_result = poll(&descriptor, 1, 2000);
    if (clock_gettime(CLOCK_MONOTONIC, &end) == -1) {
        perror("clock_gettime");
        goto stop;
    }
    if (poll_result != 1 || !(descriptor.revents & POLLIN)) {
        fprintf(stderr, "poll: result=%d revents=0x%x\n", poll_result, descriptor.revents);
        goto stop;
    }
    printf("POLL=POLLIN elapsed=%d ms\n", elapsed_ms(&start, &end));

    count = read(fd, &frame, sizeof(frame));
    if (count != (ssize_t)sizeof(frame)) {
        fprintf(stderr, "read: expected %zu bytes, got %zd (errno=%d)\n",
                sizeof(frame), count, errno);
        goto stop;
    }
    if (frame.sample_count != SPARAM_SIM_SAMPLES) {
        fprintf(stderr, "read: expected %u samples, got %u\n",
                SPARAM_SIM_SAMPLES, frame.sample_count);
        goto stop;
    }
    printf("READ=%zd bytes sequence=%" PRIu64 " samples=%u frequency=%" PRIu64 "\n",
           count, (uint64_t)frame.sequence, frame.sample_count,
           (uint64_t)frame.frequency_hz);

    if (get_status(fd, &status) == -1) goto stop;

    if (ioctl(fd, SPARAM_SIM_STOP) == -1) {
        perror("STOP");
        goto out;
    }
    printf("STOP=ok\n");

    descriptor.events = POLLIN;
    descriptor.revents = 0;
    /* STOP 应让等待读者得到挂断状态，而非无限阻塞。 */
    poll_result = poll(&descriptor, 1, 0);
    if (poll_result != 1 || !(descriptor.revents & POLLHUP)) {
        fprintf(stderr, "poll after STOP: result=%d revents=0x%x\n",
                poll_result, descriptor.revents);
        goto out;
    }
    printf("POLL_AFTER_STOP=POLLHUP\n");

    if (get_status(fd, &status) == -1) goto out;
    result = 0;
out:
    close(fd);
    return result;

stop:
    if (ioctl(fd, SPARAM_SIM_STOP) == -1)
        perror("STOP cleanup");
    goto out;
}
