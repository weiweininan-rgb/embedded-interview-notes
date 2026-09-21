#define _POSIX_C_SOURCE 200809L
/* 捕获 stderr 检查日志过滤，无需改动生产日志接口。 */
#include "log.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void read_stderr(FILE *capture, char *buffer, size_t capacity)
{
    /* 先刷新缓冲区，再读取重定向后的 stderr。 */
    size_t count;

    fflush(stderr);
    rewind(capture);
    count = fread(buffer, 1, capacity - 1, capture);
    buffer[count] = '\0';
}

static void test_level_filtering(void)
{
    FILE *capture;
    int saved_stderr;
    char output[1024];

    capture = tmpfile();
    assert(capture != NULL);
    saved_stderr = dup(STDERR_FILENO);
    assert(saved_stderr >= 0);
    assert(dup2(fileno(capture), STDERR_FILENO) >= 0);

    /* INFO 低于当前阈值；WARN 和 ERROR 必须仍然可见。 */
    log_set_level(LOG_WARN);
    LOGI("test", "hidden-info");
    LOGW("test", "visible-warning");
    LOGE("test", "visible-error");
    read_stderr(capture, output, sizeof(output));

    assert(dup2(saved_stderr, STDERR_FILENO) >= 0);
    close(saved_stderr);
    fclose(capture);

    assert(strstr(output, "hidden-info") == NULL);
    assert(strstr(output, "visible-warning") != NULL);
    assert(strstr(output, "visible-error") != NULL);
    assert(strstr(output, "[test]") != NULL);
}

static void test_debug_and_name_parsing(void)
{
    FILE *capture;
    int saved_stderr;
    char output[512];
    int ok;

    assert(log_level_from_name("warn", &ok) == LOG_WARN);
    assert(ok == 1);
    assert(log_level_from_name("bogus", &ok) == LOG_INFO);
    assert(ok == 0);

    capture = tmpfile();
    assert(capture != NULL);
    saved_stderr = dup(STDERR_FILENO);
    assert(saved_stderr >= 0);
    assert(dup2(fileno(capture), STDERR_FILENO) >= 0);

    log_set_level(LOG_DEBUG);
    LOGD("test", "visible-debug");
    read_stderr(capture, output, sizeof(output));

    assert(dup2(saved_stderr, STDERR_FILENO) >= 0);
    close(saved_stderr);
    fclose(capture);
    assert(strstr(output, "visible-debug") != NULL);
}

int main(void)
{
    test_level_filtering();
    test_debug_and_name_parsing();
    puts("log tests: PASS");
    return 0;
}
