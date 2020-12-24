#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

int 
main(int argc, char* argv[]){
    // i: index of argv
    // j: index of character(each line)
    // pid: child's pid
    // len: get length
    // first_blank: separate parameters
    int i, j, pid, len;
    int first_blank;
    char args[MAXARG][32];
    char *p[MAXARG];
    char buf;

    // lack of argv 
    if(argc < 2){
        fprintf(2, "usage: xargs <cmd> ... \n");
        exit();
    }

    while (1)
    {
        // init 
        i = 0;
        first_blank = 0;
        memset(args, 0, MAXARG * 32);

        // push the command args and stdin args together
        // the first arg must be command itself
        for(j = 1; j < argc; j++){
            strcpy(args[i++], argv[j]);
        }
        j = 0;
        // printf("now I'm here.\n");
        while(i < MAXARG - 1){
            if((len = read(0, &buf, 1)) <= 0){
                // ctrl + D
                wait();
                exit();
            }
            if(buf == '\n'){
                // printf("now turn to a new line\n");
                break;
            }
            if(buf == ' '){
                if(first_blank){
                    i++;
                    j = 0;
                    first_blank = 0;
                }
                continue;
            }
            // 将数据添加在后面
            args[i][j++] = buf;
            first_blank = 1;
        }
        // copy arg to p
        for(i = 0; i < MAXARG - 1; i++){
            p[i] = args[i];
        }
        // last arg must be 0
        p[MAXARG-1] = 0;
        
        // exec command
        if((pid = fork()) == 0){
            exec(argv[1], p);
            exit();
        }
    }
    exit();
}
