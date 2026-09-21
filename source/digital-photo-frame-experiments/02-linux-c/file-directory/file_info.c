#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * 把 st_mode 中的文件类型解析成人能看懂的文字。
 * st_mode 里既包含“类型”也包含“权限”，S_ISxxx 宏专门用来判断类型。
 */
const char *get_file_type(mode_t mode)
{
    if (S_ISREG(mode))
        return "regular file";   // 普通文件
    if (S_ISDIR(mode))
        return "directory";      // 目录
    if (S_ISLNK(mode))
        return "symlink";        // 符号链接
    if (S_ISCHR(mode))
        return "char device";    // 字符设备
    if (S_ISBLK(mode))
        return "block device";   // 块设备
    if (S_ISFIFO(mode))
        return "fifo";           // 管道文件
    if (S_ISSOCK(mode))
        return "socket";         // 套接字文件

    return "other";
}

int main(int argc, char *argv[])
{
    struct stat info;   // 用来接收 lstat() 返回的文件信息
    const char *path;

    /* 程序必须带一个路径参数 */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s PATH\n", argv[0]);
        return 1;
    }

    path = argv[1];

    /*
     * 用 lstat() 而不是 stat()：
     * 如果 PATH 本身是符号链接，lstat() 会告诉我们“它是链接”；
     * stat() 则会继续跟到它指向的目标文件。
     */
    if (lstat(path, &info) == -1) {
        perror("lstat");
        return 1;
    }

    printf("path: %s\n", path);

    /* 输出文件类型 */
    printf("type: %s\n", get_file_type(info.st_mode));

    /*
     * st_size 表示文件大小。
     * 对普通文件来说通常就是文件字节数；
     * 对目录/链接等类型，这个值存在，但含义不一定像普通文件那样直观。
     */
    printf("size: %lld bytes\n", (long long)info.st_size);

    /*
     * 只取最低 9 位权限位输出。
     * 0777 不是“魔法数字”，这里表示 rwxrwxrwx 这 9 个权限位的掩码。
     * %03o 表示按八进制输出，至少显示 3 位，例如 644、755。
     */
    printf("mode: %03o\n", info.st_mode & 0777);

    return 0;
}