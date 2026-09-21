#include <fcntl.h>  /* open()、O_RDONLY */
#include <stdio.h>  /* fprintf()、perror() */
#include <unistd.h> /* close() */

int main(int argc, char *argv[])
{
    int fd;

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
        fprintf(stderr, "无法打开文件：%s\n", argv[1]);
        return 1;
    }

    /*
    * close() 成功返回 0，失败返回 -1。
    */
    if (close(fd) == -1) {
        perror("close");
        return 1;
    }

    return 0;
}
