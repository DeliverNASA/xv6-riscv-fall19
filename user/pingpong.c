#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(){
    int parent_fd[2], child_fd[2];
    char buffer[64];
    pipe(parent_fd);
    pipe(child_fd);

    if(fork() == 0) {
        // child
        write(child_fd[1], "pong", strlen("pong"));
        close(child_fd[1]);
        read(parent_fd[0], buffer, strlen("pong"));
        printf("%d: received %s\n", getpid(), buffer);

    }else {
        // parent
        write(parent_fd[1], "ping", strlen("ping"));
        close(parent_fd[1]);
        read(child_fd[0], buffer, strlen("ping"));
        printf("%d: received %s\n", getpid(), buffer);
    }
    exit();
}
