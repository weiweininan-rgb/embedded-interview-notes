#define _POSIX_C_SOURCE 200809L
/* 带时间戳的最小 stderr 日志，不干扰测量主路径。 */
#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static enum log_level current_level = LOG_INFO;

void log_set_level(enum log_level level)
{
    current_level = level;
}

enum log_level log_level_from_name(const char *text, int *ok)
{
    /* 同时返回安全默认值和有效性标记，供配置解析使用。 */
    if (ok) *ok = 1;
    if (!strcmp(text, "debug")) return LOG_DEBUG;
    if (!strcmp(text, "info")) return LOG_INFO;
    if (!strcmp(text, "warn")) return LOG_WARN;
    if (!strcmp(text, "error")) return LOG_ERROR;
    if (ok) *ok = 0;
    return LOG_INFO;
}

void log_message(enum log_level level, const char *module,
                 const char *format, ...)
{
    static const char *names[] = { "DEBUG", "INFO", "WARN", "ERROR" };
    struct timespec now;
    struct tm local;
    char timestamp[32];
    va_list arguments;

    /* 数字越小越详细，低于当前阈值的日志直接丢弃。 */
    if (level < current_level)
        return;
    clock_gettime(CLOCK_REALTIME, &now);
    localtime_r(&now.tv_sec, &local);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &local);
    fprintf(stderr, "%s.%03ld %-5s [%s] ", timestamp, now.tv_nsec / 1000000L,
            names[level], module ? module : "main");
    /* 保持 printf 风格接口，调用处无需重复格式化代码。 */
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
}
