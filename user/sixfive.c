#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

char decimal_str[512];



int is_sep(char c)
{
    char* s = strchr(" -\r\t\n./,", c);

    return !!s;
}

int is_decimal(char c)
{
    if (c >= '0' && c <= '9')
        return 1;
    else
        return 0;
}

void print_sixfive(int n)
{
    if (!(n%6) || !(n%5))
        fprintf(2, "%d\n", n);
}

void
sixfive(int fd)
{
    char c;

    memset(decimal_str, 0, sizeof(decimal_str));

    //start of file is implicit separaotr
    int front_sep = 1;
    int decimal_ptr = 0;

    while(read(fd, &c, 1) > 0) {
        if (is_sep(c)) { //sepator
            if (!front_sep)
                front_sep = 1;
            else {
                if (decimal_ptr) {
                    decimal_str[decimal_ptr] = '\0';
                    //fprintf(2, "decimal str: %s\n", decimal_str);
                    int n = atoi(decimal_str);
                    print_sixfive(n);
                    decimal_ptr = 0;
                    front_sep = 1; //next possible prefix match
                } else {
                    continue;
                }
            }
        } else if (is_decimal(c)) { //decimal
            if (!front_sep)
                continue;
            else {
                decimal_str[decimal_ptr++] = c;
            }
        } else { // ! (separaotr | decimal)
            if (!front_sep)
                continue;
            else {
                front_sep = 0;
                decimal_ptr = 0;
            }
        }

    } //while

    //end of file is implicit separaotr
    if (decimal_ptr && front_sep)
        print_sixfive(atoi(decimal_str));
}

    int
main(int argc, char *argv[])
{
    int i, fd;

    if(argc <= 1){
        exit(0);
    }

    for(i = 1; i < argc; i++){
        if((fd = open(argv[i], O_RDONLY)) < 0){
            fprintf(2, "sixfive: cannot open %s\n", argv[i]);
            exit(1);
        }
        sixfive(fd);
        close(fd);
    }
    exit(0);
}
