/*
 * crt_start.c — _start → BlocksCrtCallMain：挂 environ 再调 main
 *
 * 栈：argc | argv… | NULL | envp… | NULL
 * environ = argv + argc + 1（即 envp）
 */
#include <stdlib.h>

extern int main(int Argc, char **Argv);

int BlocksCrtCallMain(int Argc, char **Argv) {
    if (Argc < 0) {
        Argc = 0;
    }
    if (Argv) {
        environ = Argv + Argc + 1;
    } else {
        environ = 0;
    }
    return main(Argc, Argv);
}
