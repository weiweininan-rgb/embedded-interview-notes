/*
 * file_copy.c
 *
 * 功能：完整复制文本文件、空文件或二进制文件。
 *
 * 使用方法：
 *   ./file_copy SOURCE TARGET
 */

#include <errno.h>   /* errno、EINTR */
#include <fcntl.h>   /* open()、O_RDONLY 等 */
#include <stdio.h>   /* fprintf()、perror() */
#include <unistd.h>  /* read()、write()、close() */

/*
 * 把 buffer 中的 count 字节可靠地全部写入 fd。
 *
 * write() 成功时不保证一次写完所有数据，
 * 所以必须循环，直到 count 字节全部写完。
 */
static int write_all(int fd, const char *buffer, size_t count)
{
    /* 记录已经成功写入多少字节 */
    size_t total_written = 0;

    while (total_written < count) {
        ssize_t n;

        /*
         * buffer + total_written：
         * 从尚未写入的位置继续写。
         *
         * count - total_written：
         * 还剩多少字节需要写入。
         */
        n = write(fd,
                  buffer + total_written,
                  count - total_written);

        if (n == -1) {
            /*
             * EINTR 表示 write() 被信号临时打断，
             * 此时重新执行 write() 即可。
             */
            if (errno == EINTR) {
                continue;
            }

            return -1;
        }

        /*
         * 正常文件通常不会出现 write() 返回 0。
         * 加上检查可以避免循环永远无法结束。
         */
        if (n == 0) {
            errno = EIO;
            return -1;
        }

        total_written += (size_t)n;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int source_fd;
    int target_fd;
    char buffer[4096];
    ssize_t n;
    int result = 0;

    /*
     * 正确调用：
     *   ./file_copy SOURCE TARGET
     *
     * argv[0] = "./file_copy"
     * argv[1] = 源文件路径
     * argv[2] = 目标文件路径
     */
    if (argc != 3) {
        fprintf(stderr, "Usage: %s SOURCE TARGET\n", argv[0]);
        return 1;
    }

    /*
     * 必须先打开源文件。
     *
     * 这样当源文件不存在时，程序会立刻退出，
     * 不会错误地创建目标文件。
     */
    source_fd = open(argv[1], O_RDONLY);

    if (source_fd == -1) {
        perror("open source");
        return 1;
    }

    /*
     * 打开目标文件：
     *
     * O_WRONLY：只写
     * O_CREAT ：不存在时创建
     * O_TRUNC ：存在时清空旧内容
     *
     * 0644 是创建新文件时使用的权限：
     * 所有者可以读写，其他用户只读。
     */
    target_fd = open(argv[2],
                     O_WRONLY | O_CREAT | O_TRUNC,
                     0644);

    if (target_fd == -1) {
        perror("open target");

        /* 源文件已经打开，因此退出前需要关闭 */
        if (close(source_fd) == -1) {
            perror("close source");
        }

        return 1;
    }

    /*
     * 循环读取源文件。
     *
     * read() 返回：
     *   n > 0：本次实际读取了 n 字节
     *   n = 0：到达文件末尾
     *   n = -1：读取失败
     */
    while ((n = read(source_fd, buffer, sizeof(buffer))) > 0) {
        /*
         * 可靠地把本轮读到的 n 字节写入目标文件。
         *
         * 不能使用 strlen(buffer)，因为：
         * 1. buffer 不一定以 '\0' 结尾；
         * 2. 二进制数据中可能包含 '\0'；
         * 3. n 才是本次真正读取的字节数。
         */
        if (write_all(target_fd, buffer, (size_t)n) == -1) {
            perror("write target");
            result = 1;
            break;
        }
    }

    /*
     * 如果 read() 返回 -1，说明复制过程发生读取错误。
     *
     * 如果之前是 write() 失败，n 仍然大于 0，
     * 所以这里不会重复报告 read 错误。
     */
    if (n == -1) {
        perror("read source");
        result = 1;
    }

    /*
     * 两个文件描述符都已经打开，
     * 无论复制成功还是失败，都必须关闭。
     */
    if (close(source_fd) == -1) {
        perror("close source");
        result = 1;
    }

    if (close(target_fd) == -1) {
        perror("close target");
        result = 1;
    }

    return result;
}