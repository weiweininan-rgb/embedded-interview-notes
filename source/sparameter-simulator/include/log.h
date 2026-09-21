#ifndef SPARAM_LOG_H
#define SPARAM_LOG_H

/* 简单 stderr 日志器，进程内统一按最低级别过滤。 */
enum log_level { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };
void log_message(enum log_level level, const char *module,
                 const char *format, ...);
void log_set_level(enum log_level level);
enum log_level log_level_from_name(const char *text, int *ok);

/* 调用处快捷宏，让日志级别一眼可见。 */
#define LOGD(module, ...) log_message(LOG_DEBUG, module, __VA_ARGS__)
#define LOGI(module, ...) log_message(LOG_INFO, module, __VA_ARGS__)
#define LOGW(module, ...) log_message(LOG_WARN, module, __VA_ARGS__)
#define LOGE(module, ...) log_message(LOG_ERROR, module, __VA_ARGS__)

#endif
