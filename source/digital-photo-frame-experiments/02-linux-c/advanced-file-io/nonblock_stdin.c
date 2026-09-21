#include <errno.h>  /* errno、EAGAIN、EWOULDBLOCK */
#include <fcntl.h>  /* fcntl()、F_GETFL、F_SETFL、O_NONBLOCK */
#include <stdio.h>  /* printf()、perror() */
#include <unistd.h> /* read()、STDIN_FILENO */

int main(void)
{
    int original_flags;
    int result = 0;
    char buffer[128];
    ssize_t n;

    /*
     * 获取标准输入当前的文件状态标志。
     * STDIN_FILENO 就是标准输入的文件描述符 0。
     */
    original_flags = fcntl(STDIN_FILENO, F_GETFL);
    if (original_flags == -1) {
        perror("fcntl F_GETFL");
        return 1;
    }

    /*
     * 保留原来的状态标志，并添加 O_NONBLOCK。
     * 添加后，read() 没有数据时不会一直等待。
     */
    if (fcntl(STDIN_FILENO,
              F_SETFL,
              original_flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
        return 1;
    }

    /*
     * 尝试从标准输入读取数据。
     *
     * n > 0 ：成功读取了 n 字节
     * n == 0：到达 EOF
     * n == -1：暂时没有数据，或者真正发生错误
     */
    n = read(STDIN_FILENO, buffer, sizeof(buffer));

    if (n > 0) {
        printf("读取到 %zd 字节数据\n", n);
    } else if (n == 0) {
        printf("到达 EOF\n");
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        /*
         * 非阻塞读取时，暂时没有数据是正常状态，
         * 不是设备损坏，因此不调用 perror()。
         */
        printf("当前暂时没有输入数据\n");
    } else {
        /* 其他 errno 表示真正的读取错误。 */
        perror("read");
        result = 1;
    }

    /*
     * 恢复标准输入原来的状态标志，
     * 避免非阻塞属性继续影响后续使用者。
     */
    if (fcntl(STDIN_FILENO,
              F_SETFL,
              original_flags) == -1) {
        perror("fcntl restore");
        result = 1;
    }

    return result;
}