#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

int main(int argc, char *argv[])
{
    const char *dir_path;
    DIR *dir;
    struct dirent *entry;
   char full_path[1024];
    struct stat info;
    int ret;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s DIRECTORY\n", argv[0]);
        return 1;
    }

    dir_path = argv[1];

    /* 打开目录，成功后返回目录流指针 DIR * */
    dir = opendir(dir_path);
    if (dir == NULL) {
        perror("opendir");
        return 1;
    }

    /*
     * readdir() 每次返回一个目录项。
     * 返回 NULL 有两种可能：
     * 1. 目录读完了
     * 2. 读取出错了
     * 所以循环前先把 errno 清零，后面才能区分。
     */
    errno = 0;

    while ((entry = readdir(dir)) != NULL) {
        /* 跳过当前目录 . 和上级目录 .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        /*
         * 拼接完整路径。
         * 不能直接对 d_name 做 lstat()，因为 d_name 只是“名字”，
         * 不是完整路径；程序当前工作目录未必就是目标目录。
         */
        ret = snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        if (ret < 0 || ret >= (int)sizeof(full_path)) {
            fprintf(stderr, "path too long: %s/%s\n", dir_path, entry->d_name);
            closedir(dir);
            return 1;
        }

        /* 用 lstat() 获取该目录项的文件信息 */
        if (lstat(full_path, &info) == -1) {
            perror("lstat");
            closedir(dir);
            return 1;
        }

        /* 根据 st_mode 判断类型 */
        if (S_ISDIR(info.st_mode))
            printf("[DIR] %s\n", entry->d_name);
        else if (S_ISREG(info.st_mode))
            printf("[FILE] %s\n", entry->d_name);
        else
            printf("[OTHER] %s\n", entry->d_name);

        errno = 0;
    }

    /* 如果循环结束时 errno 不是 0，说明不是读完，而是出错 */
    if (errno != 0) {
        perror("readdir");
        closedir(dir);
        return 1;
    }

    /* 用完目录流后要释放 */
    if (closedir(dir) == -1) {
        perror("closedir");
        return 1;
    }

    return 0;
}