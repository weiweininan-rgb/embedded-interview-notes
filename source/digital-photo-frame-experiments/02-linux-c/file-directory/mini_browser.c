#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#define PATH_BUF_SIZE 4096
#define LINE_BUF_SIZE 256

/* 去掉 fgets() 读进来的换行符 */
static void trim_newline(char *s)
{
    size_t len = strlen(s);

    if (len > 0 && s[len - 1] == '\n')
        s[len - 1] = '\0';
}

/*
 * 把 base 和 name 拼成完整路径。
 * 这里只做最基本的拼接，不引入复杂路径规范化。
 */
static int join_path(char *dst, size_t dst_size, const char *base, const char *name)
{
    int ret;

    if (strcmp(base, "/") == 0)
        ret = snprintf(dst, dst_size, "/%s", name);
    else if (strcmp(base, ".") == 0)
        ret = snprintf(dst, dst_size, "%s", name);
    else
        ret = snprintf(dst, dst_size, "%s/%s", base, name);

    if (ret < 0 || ret >= (int)dst_size)
        return -1;

    return 0;
}

/* 回到上一级目录，只修改我们自己维护的 current_path 字符串 */
static void go_parent(char *path)
{
    char *slash;

    if (strcmp(path, "/") == 0)
        return;

    slash = strrchr(path, '/');
    if (slash == NULL) {
        strcpy(path, ".");
        return;
    }

    /* 如果是 /xxx 这种绝对路径，回到根目录时保留开头的 / */
    if (slash == path) {
        path[1] = '\0';
        return;
    }

    *slash = '\0';
}

/* 列出当前目录内容，并标注文件类型 */
static void list_dir(const char *dir_path)
{
    DIR *dir;
    struct dirent *entry;
    struct stat info;
    char full_path[PATH_BUF_SIZE];
    int ret;

    dir = opendir(dir_path);
    if (dir == NULL) {
        perror("opendir");
        return;
    }

    /*
     * readdir() 返回 NULL 有两种可能：
     * 1. 目录已经读完了
     * 2. 读取出错了
     * 所以要先清 errno，再在循环结束后检查它。
     */
    errno = 0;

    while ((entry = readdir(dir)) != NULL) {
        /* 跳过当前目录和上级目录 */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        /* 先拼出完整路径，后面才能对这个条目做 stat/lstat */
        ret = join_path(full_path, sizeof(full_path), dir_path, entry->d_name);
        if (ret == -1) {
            fprintf(stderr, "path too long: %s/%s\n", dir_path, entry->d_name);
            closedir(dir);
            return;
        }

        /*
         * 用 lstat() 看“条目本身”的类型。
         * 这样符号链接会被识别成链接，而不是跟着链接去看目标。
         */
        if (lstat(full_path, &info) == -1) {
            perror("lstat");
            closedir(dir);
            return;
        }

        if (S_ISDIR(info.st_mode))
            printf("[DIR] %s\n", entry->d_name);
        else if (S_ISREG(info.st_mode))
            printf("[FILE] %s\n", entry->d_name);
        else
            printf("[OTHER] %s\n", entry->d_name);

        errno = 0;
    }

    if (errno != 0) {
        perror("readdir");
        closedir(dir);
        return;
    }

    if (closedir(dir) == -1)
        perror("closedir");
}

/* 打印帮助信息 */
static void print_help(void)
{
    printf("Commands:\n");
    printf("  ls           list current directory\n");
    printf("  cd <dir>     enter subdirectory\n");
    printf("  up           go to parent directory\n");
    printf("  pwd          print current path\n");
    printf("  quit         exit program\n");
}

int main(void)
{
    char current_path[PATH_BUF_SIZE];
    char line[LINE_BUF_SIZE];
    char next_path[PATH_BUF_SIZE];
    struct stat info;

    if (getcwd(current_path, sizeof(current_path)) == NULL) {
        perror("getcwd");
        strcpy(current_path, ".");
    }

    print_help();

    while (1) {
        printf("%s> ", current_path);
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL)
            break;

        trim_newline(line);

        if (line[0] == '\0')
            continue;

        if (strcmp(line, "ls") == 0) {
            list_dir(current_path);
        } else if (strcmp(line, "pwd") == 0) {
            printf("%s\n", current_path);
        } else if (strcmp(line, "up") == 0 || strcmp(line, "cd ..") == 0) {
            go_parent(current_path);
        } else if (strncmp(line, "cd ", 3) == 0) {
            const char *name = line + 3;

            if (name[0] == '\0') {
                fprintf(stderr, "cd: missing operand\n");
                continue;
            }

            if (join_path(next_path, sizeof(next_path), current_path, name) == -1) {
                fprintf(stderr, "path too long: %s/%s\n", current_path, name);
                continue;
            }

            /*
             * 用 stat() 判断目标是不是目录。
             * 这里允许进入“指向目录的符号链接”。
             */
            if (stat(next_path, &info) == -1) {
                perror("stat");
                continue;
            }

            if (!S_ISDIR(info.st_mode)) {
                fprintf(stderr, "cd: not a directory: %s\n", name);
                continue;
            }

            snprintf(current_path, sizeof(current_path), "%s", next_path);
        } else if (strcmp(line, "help") == 0) {
            print_help();
        } else if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
            break;
        } else {
            fprintf(stderr, "unknown command: %s\n", line);
        }
    }

    return 0;
}