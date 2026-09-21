/*
 * write_log.c
 *
 * 功能：把一条消息追加到指定日志文件末尾。
 *
 * 使用示例：
 *   ./write_log app.log "button pressed"
 */

#include <errno.h>   /* 提供 errno */
#include <fcntl.h>   /* 提供 open() 和 O_* 标志 */
#include <stdio.h>   /* 提供 fprintf()、perror() */
#include <string.h>  /* 提供 strlen() */
#include <unistd.h>  /* 提供 write()、close() */

/*
 * 把 length 字节的数据完整写入文件。
 *
 * write() 有时可能只写入一部分数据，因此使用循环，
 * 直到全部写完，或者发生错误。
 */
static int write_all(int fd, const char *data, size_t length)
{
    size_t written = 0;

    while (written < length) {
        ssize_t n = write(fd, data + written, length - written);

        if (n == -1) {
            /*
             * EINTR 表示 write() 被信号临时打断，
             * 这种情况可以继续重试。
             */
            if (errno == EINTR) {
                continue;
            }

            return -1;
        }

        written += (size_t)n;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int fd;

    /*
     * 正确调用需要三个参数：
     *
     * argv[0] = "./write_log"
     * argv[1] = "app.log"
     * argv[2] = "button pressed"
     */
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <log-file> <message>\n", argv[0]);
        return 1;
    }

    /*
     * 打开日志文件。
     *
     * O_WRONLY：只写方式打开
     * O_CREAT ：文件不存在时自动创建
     * O_APPEND：每次都从文件末尾追加，不覆盖旧内容
     *
     * 0644 是新文件的权限：
     * 文件所有者可以读写，其他用户只能读取。
     */
    fd = open(argv[1], O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (fd == -1) {
        perror("open");
        return 1;
    }

    /* 先把用户提供的消息写入日志 */
    if (write_all(fd, argv[2], strlen(argv[2])) == -1) {
        perror("write");
        close(fd);
        return 1;
    }

    /* 再写入换行符，保证每条消息单独占一行 */
    if (write_all(fd, "\n", 1) == -1) {
        perror("write");
        close(fd);
        return 1;
    }

    /* 日志写入完成，关闭文件 */
    if (close(fd) == -1) {
        perror("close");
        return 1;
    }

    return 0;
}