#include "kernel/types.h"
#include "user/user.h"

// select 
void SelectData(int p, int fd_in, int fd_out) {
    int n;
    while(read(fd_in, &n, sizeof(int))){
        if(n % p != 0){
            write(fd_out, &n, sizeof(int));
        }
    }
}

// prime
void Prime(int fd_in) {
    int divide;
    int cur_fd[2];
    pipe(cur_fd);

    if(read(fd_in, &divide, sizeof(int)) <= 0){
        exit();
    }

    printf("prime %d\n", divide);

    if(fork() == 0) {
        // child
        SelectData(divide, fd_in, cur_fd[1]);
        exit();
    }else {
        // parent
        wait();
        close(cur_fd[1]);
        Prime(cur_fd[0]);
    }
}

void main(int argc, char *argv[]){
    int fd[2];
    int i;
    
    // init
    pipe(fd);
    for(i = 2; i < 35; i++){
        write(fd[1], &i, sizeof(int));
    }

    close(fd[1]);
    Prime(fd[0]);
}
