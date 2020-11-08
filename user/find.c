#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char*
fmtname(char *path){
    static char buf[DIRSIZ+1];
    char *p;

    // Find first character after last slash
    for(p = path + strlen(path); p >= path && *p != '/'; p--);
    p++;

    // Return blank-padded name.
    if(strlen(p) >= DIRSIZ){
        return p;
    }
    // move p to buffer
    memmove(buf, p, strlen(p));
    // memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
    buf[strlen(p)] = 0;
    return buf;
}

// compare file names
void 
find(char* path, char* name){
    char buf[512], *p;
    int fd; 
    struct dirent de;
    struct stat st;

    // can't open 
    if((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        exit();
    }
    
    // can't stat
    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        exit();
    }

    switch (st.type){
        // if file, output result
        case T_FILE:
            if(strcmp(fmtname(path), name) == 0){
                // printf("name = %s\n", name);
                printf("%s\n", path);
            }
            break;
        // if dir, move forward
        case T_DIR:
            if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)){
                fprintf(2, "find: path is too long\n");
                break;
            }
            // add current path
            strcpy(buf, path);
            p = buf + strlen(buf);
            *p++ = '/';

            // not the end
            while(read(fd, &de, sizeof(de)) == sizeof(de)){
                if(de.inum == 0){
                    continue;
                }

                // add de.name to path
                memmove(p, de.name, DIRSIZ);
                p[DIRSIZ] = 0;

                // avoid . and ..
                if(!strcmp(de.name, ".") || !strcmp(de.name, "..")) continue;
                find(buf, name);
            }
            break;
    }
    close(fd);
}

int 
main(int argc, char* argv[]){
    if(argc != 3){
        fprintf(2, "usage: find <path> <name>\n");
        exit();
    }
    find(argv[1], argv[2]);
    exit();
}
