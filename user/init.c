#include "lib.h"
#include "syscall.h"
#include <stdint.h>

static int read_line(char* buf, int max_len) {
    int pos = 0;
    while (pos < max_len - 1) {
        char c = 0;
        int64_t n = sys_read(0, &c, 1);
        if (n <= 0) continue;
        if (c == '\n') break;
        buf[pos++] = c;
    }
    buf[pos] = 0;
    return pos;
}

static int split_args(char* line, char* argv[], int max_args) {
    int argc = 0;
    char* p = line;
    while (*p && argc < max_args) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) {
            *p = 0;
            p++;
        }
    }
    return argc;
}

static void cmd_ls(const char* path) {
    const char* target = path ? path : "/";
    int fd = (int)sys_open(target, O_RDONLY);
    if (fd < 0) {
        printf("ls: cannot open %s\n", target);
        return;
    }

    char name[256];
    while (1) {
        int64_t n = sys_readdir(fd, name, sizeof(name));
        if (n <= 0) break;
        printf("%s\n", name);
    }
    sys_close(fd);
}

static void cmd_cat(const char* path) {
    if (!path) {
        printf("cat: missing file\n");
        return;
    }
    int fd = (int)sys_open(path, O_RDONLY);
    if (fd < 0) {
        printf("cat: cannot open %s\n", path);
        return;
    }

    char buf[256];
    while (1) {
        int64_t n = sys_read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        sys_write(1, buf, n);
    }
    sys_close(fd);
}

static void run_external(char* path) {
    int64_t pid = sys_fork();
    if (pid == 0) {
        if (sys_execve(path) < 0) {
            printf("exec: failed to run %s\n", path);
            sys_exit(1);
        }
        sys_exit(0);
    } else if (pid > 0) {
        int status = 0;
        sys_waitpid(pid, &status);
    } else {
        printf("fork failed\n");
    }
}

int main(void) {
    char line[256];
    char* argv[8];

    puts("MyOS user shell. Type 'help' for commands.");

    while (1) {
        printf("myos> ");
        int len = read_line(line, sizeof(line));
        if (len == 0) continue;

        int argc = split_args(line, argv, 8);
        if (argc == 0) continue;

        if (strcmp(argv[0], "help") == 0) {
            puts("Built-ins: help ls cat exit");
        } else if (strcmp(argv[0], "ls") == 0) {
            cmd_ls(argc > 1 ? argv[1] : "/");
        } else if (strcmp(argv[0], "cat") == 0) {
            cmd_cat(argc > 1 ? argv[1] : 0);
        } else if (strcmp(argv[0], "exit") == 0) {
            break;
        } else {
            run_external(argv[0]);
        }
    }

    sys_exit(0);
    return 0;
}
