#include <Blocks/Process.h>
/*
 * CwdDemo.c — getcwd / chdir / wait 退出码
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    char Buf[64];
    int St;
    pid_t Pid;

    if (!getcwd(Buf, sizeof(Buf))) {
        printf("cwd: getcwd fail\n");
        return 1;
    }
    printf("cwd: %s\n", Buf);
    if (chdir("Assets") != 0) {
        printf("cwd: chdir Assets fail\n");
        return 1;
    }
    if (!getcwd(Buf, sizeof(Buf))) {
        printf("cwd: getcwd2 fail\n");
        return 1;
    }
    printf("cwd: %s\n", Buf);
    Pid = ProcessFork();
    if (Pid < 0) {
        printf("cwd: fork fail\n");
        return 1;
    }
    if (Pid == 0) {
        exit(42);
    }
    St = 0;
    if (ProcessWait(&St) < 0) {
        printf("cwd: wait fail\n");
        return 1;
    }
    printf("cwd: exit=%d\n", WEXITSTATUS(St));
    return WEXITSTATUS(St) == 42 ? 0 : 1;
}
