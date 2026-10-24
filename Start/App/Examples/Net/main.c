/*
 * Net — TCP 客户端；Guest 需 lwip on；宿主机 nc -l -p 8888
 */
#include <stdio.h>
#include <Net.h>
#include <Blocks/File.h>

int main(void) {
    int Fd;

    Fd = socket(AF_INET, SOCK_STREAM, 0);
    if (Fd < 0) {
        printf("mynet: socket fail\n");
        return 1;
    }
    if (NetConnect(Fd, NetIpv4(10, 0, 2, 2), 8888) != 0) {
        printf("mynet: connect fail\n");
        FileClose(Fd);
        return 1;
    }
    send(Fd, "hi\n", 3, 0);
    FileClose(Fd);
    printf("mynet: ok\n");
    return 0;
}
