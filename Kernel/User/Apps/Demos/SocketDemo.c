/*
 * SockDemo.c — NetConnect 冒烟（selfhost：无 sockaddr）
 * Guest：lwip on；宿主 nc -l -p 8888；exec SOCKDEMO.ELF
 */
#include <stdio.h>
#include <string.h>
#include <ToyNet.h>
#include <Blocks/File.h>

int main(void) {
    int Fd;
    const char *Msg = "from sockdemo!\n";
    char Buf[64];
    ssize_t N;

    printf("sockdemo: Net %s\n", NET_ABI_VERSION_STRING);

    Fd = socket(AF_INET, SOCK_STREAM, 0);
    if (Fd < 0) {
        printf("sockdemo: socket fail\n");
        return 1;
    }

    if (NetConnect(Fd, NetIpv4(10, 0, 2, 2), 8888) != 0) {
        printf("sockdemo: connect fail\n");
        FileClose(Fd);
        return 1;
    }
    if (send(Fd, Msg, strlen(Msg), 0) <= 0) {
        printf("sockdemo: send fail\n");
        FileClose(Fd);
        return 1;
    }
    N = recv(Fd, Buf, sizeof(Buf) - 1, 0);
    FileClose(Fd);
    if (N > 0) {
        Buf[N] = 0;
        printf("sockdemo: ok recv=%s", Buf);
    } else {
        printf("sockdemo: ok\n");
    }
    return 0;
}
