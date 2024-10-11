#include "kernel/types.h"
#include "user.h"

#define BUFSIZE 512  

int main(int argc, char* argv[]) {
    if (argc != 1) {
        printf("Pingpone don't need argement!\n");
        exit(-1);
    }

    char buf[BUFSIZE];
    int f2c[2];
    int c2f[2];

    if (pipe(c2f) < 0 || pipe(f2c) < 0) {
        printf("pipe Error");
        exit(-1);
    }

    int ppid = getpid();
    int pid = fork();
    if (pid == 0) {
        // 子进程
        close(f2c[1]);
        read(f2c[0], buf, BUFSIZE);
        printf("%d: received %s from pid %d\n", getpid(), buf, ppid);
        close(f2c[0]);

        close(c2f[0]);
        write(c2f[1], "pong", BUFSIZE);
        close(c2f[1]);
    } else if (pid > 0) {
        // 父进程
        close(f2c[0]);
        write(f2c[1], "ping", BUFSIZE);
        close(f2c[1]);

        close(c2f[1]);
        read(c2f[0], buf, BUFSIZE);
        printf("%d: received %s from pid %d\n", getpid(), buf, pid);
        close(c2f[0]);
        
    } else {
        printf("fork Error");
        exit(-1);
    }
    exit(0);
}