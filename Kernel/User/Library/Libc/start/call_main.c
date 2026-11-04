/*
 * call_main.c — _start → blocks_call_main：挂 environ 再调 main
 *
 * 栈：argc | argv… | NULL | envp… | NULL
 * environ = argv + argc + 1（即 envp）
 */
#include <stdlib.h>

extern int main(int argc, char **argv);

int blocks_call_main(int argc, char **argv) {
    if (argc < 0) {
        argc = 0;
    }
    if (argv) {
        environ = argv + argc + 1;
    } else {
        environ = 0;
    }
    return main(argc, argv);
}
