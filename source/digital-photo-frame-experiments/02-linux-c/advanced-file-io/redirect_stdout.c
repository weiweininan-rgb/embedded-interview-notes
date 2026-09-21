#include <fcntl.h>  /* open()、O_WRONLY、O_CREAT、O_TRUNC */
#include <stdio.h>  /* printf()、fprintf()、perror()、fflush() */
#include <unistd.h> /* dup2()、close()、STDOUT_FILENO */

int main(int argc, char *argv[])
{
    int file_fd;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <output-file>\n", argv[0]);
        return 1;
    }

    /*
     * 以只写方式打开目标文件。
     *
     * O_CREAT：文件不存在时创建
     * O_TRUNC：文件存在时清空原内容
     * 0644：所有者可读写，其他用户只读
     */
    file_fd = open(argv[1],
                   O_WRONLY | O_CREAT | O_TRUNC,
                   0644);

    if (file_fd == -1) {
        perror("open");
        return 1;
    }

    /*
     * 让标准输出 fd 1 指向 file_fd 对应的文件。
     * 此后 printf() 将写入文件，而不是终端。
     */
    if (dup2(file_fd, STDOUT_FILENO) == -1) {
        perror("dup2");
        close(file_fd);
        return 1;
    }

    /*
     * dup2() 已经创建了新的描述符 1，
     * 所以原来的 file_fd 可以关闭。
     */
    if (close(file_fd) == -1) {
        perror("close");
        return 1;
    }

    if (printf("hello from redirected stdout\n") < 0) {
        perror("printf");
        return 1;
    }

    /*
     * printf() 有缓冲机制，内容可能暂存在内存中。
     * fflush() 强制把缓冲数据写入文件。
     */
    if (fflush(stdout) == EOF) {
        perror("fflush");
        return 1;
    }

    return 0;
}