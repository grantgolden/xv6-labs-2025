#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"

void
find(char *path, char *file_name, int argc, char *argv[])
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;
    char *ecmd[MAXARG];


    if((fd = open(path, O_RDONLY)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch(st.type){
    case T_DEVICE:
    case T_FILE:
        //get file name
        for(p=path+strlen(path); p >= path && *p != '/'; p--)
            ;
        p++;
        if (strcmp(p, file_name) == 0) {
            if (!argv)
                printf("%s\n", path);
            else {
                strcpy(buf, path);
                int i;
                for (i = 0; i < argc; i++)
                    ecmd[i] = argv[i];
                ecmd[i] = buf;

                int  pid, status;
                pid = fork();
                if (pid == 0) {
                    exec(ecmd[0], ecmd);
                    exit(0);
                } else {
                    wait(&status);
                }
            }
        }
        break;

    case T_DIR:
        if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
            printf("find: path too long\n");
            break;
        }
        strcpy(buf, path);
        p = buf+strlen(buf);
        *p++ = '/';
        while(read(fd, &de, sizeof(de)) == sizeof(de)){
            if(de.inum == 0)
                continue;
            if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                continue;
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            if(stat(buf, &st) < 0){
                printf("find: cannot stat %s\n", buf);
                continue;
            }
            find(buf, file_name, argc, argv);
        }
        break;
    }
    close(fd);
}

int
main(int argc, char *argv[])
{
    char *cmd[MAXARG];
    int i;

    if(argc < 3){
        printf("find: no available path or files\n");
        exit(1);
    } else if (argc >= 5) {
        if (strcmp(argv[3], "-exec") == 0) {
            for (i = 0; (i+4) < argc; i++)
                cmd[i] = argv[i+4];
            find(argv[1], argv[2], i, cmd);
        }
    } else
            find(argv[1], argv[2], 0, 0);


    exit(0);
}
