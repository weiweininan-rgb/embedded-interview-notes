/*
 * open_read.c
 *
 * 功能：
 * 打开命令行指定的文件，循环读取文件内容，
 * 再把读取到的内容输出到终端。
 *
 * 运行示例：
 *   ./open_read test.txt
 *
 * 这相当于一个简化版的 cat 命令。
 */

/* 提供 open() 和 O_RDONLY */
#include <fcntl.h>

/* 提供 fprintf()、perror() 和 stderr */
#include <stdio.h>

/* 提供 read()、write()、close() 和 STDOUT_FILENO */
#include <unistd.h>

/*
 * argc：命令行参数的数量。
 * argv：保存命令行参数的字符串数组。
 *
 * 执行 ./open_read test.txt 时：
 * argc    = 2
 * argv[0] = "./open_read"
 * argv[1] = "test.txt"
 */
int main(int argc, char *argv[])
{
    /*
     * fd 用于保存文件描述符。
     * 文件描述符可以理解为 Linux 为已打开文件分配的编号。
     */
    int fd;

    /*
     * n 用于保存 read() 或 write() 的返回值。
     *
     * n > 0：成功处理了 n 个字节
     * n = 0：read() 到达文件末尾
     * n = -1：发生错误
     */
    ssize_t n;

    /*
     * 临时缓冲区，每次最多从文件中读取 128 字节。
     * 文件超过 128 字节时，程序会通过循环分多次读取。
     */
    char buffer[128];

    /*
     * 要求用户必须提供一个文件名。
     *
     * 正确格式：
     *   ./open_read test.txt
     *
     * 程序名也算一个参数，所以 argc 应该等于 2。
     */
    if (argc != 2) {
        /*
         * stderr 表示标准错误输出。
         * %s 会被 argv[0]（程序名称）替换。
         */
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);

        /* 返回非 0 值，表示程序执行失败 */
        return 1;
    }

    /*
     * 打开 argv[1] 指定的文件。
     *
     * argv[1]：用户提供的文件名，例如 test.txt
     * O_RDONLY：以只读方式打开，不修改文件
     *
     * 成功：返回一个大于或等于 0 的文件描述符
     * 失败：返回 -1
     */
    fd = open(argv[1], O_RDONLY);

    /* 检查文件是否打开失败 */
    if (fd == -1) {
        /*
         * 输出 open() 失败的具体原因。
         *
         * 例如：
         * open: No such file or directory
         */
        perror("open");
        return 1;
    }

    /*
     * 循环读取文件。
     *
     * read() 的三个参数：
     *   fd             从哪个文件读取
     *   buffer         把读取的数据保存到哪里
     *   sizeof(buffer) 本次最多读取多少字节
     *
     * read() 的返回值：
     *   大于 0：本次实际读取的字节数
     *   等于 0：已经到达文件末尾
     *   等于 -1：读取失败
     */
    while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
        /*
         * 将刚刚读取的数据输出到终端。
         *
         * STDOUT_FILENO：标准输出，通常就是当前终端
         * buffer：要输出的数据
         * n：本次实际读取到的字节数
         *
         * 这里只输出 n 字节，而不是固定输出 128 字节。
         */
        write(STDOUT_FILENO, buffer, n);
    }

    /*
     * while 循环结束有两种可能：
     *
     * n == 0：正常到达文件末尾
     * n == -1：read() 读取失败
     */
    if (n == -1) {
        /* 输出 read() 失败的具体原因 */
        perror("read");

        /* 文件之前已经打开，退出前需要关闭 */
        close(fd);

        return 1;
    }

    /*
     * 文件读取完成，关闭文件描述符，释放系统资源。
     */
    close(fd);

    /* 返回 0，表示程序正常执行完成 */
    return 0;
}