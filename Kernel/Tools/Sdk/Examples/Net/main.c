/*
 * BlocksSDK Examples/Net — 链 libToyNet（Guest 需 lwip on）
 */
#include <stdio.h>
#include <unistd.h>
#include <ToyNet.h>

int main(void) {
    int Fd = socket(AF_INET, SOCK_STREAM, 0);
    if (Fd < 0) {
        printf("mynet: socket fail\n");
        return 1;
    }
    if (NetConnect(Fd, NetIpv4(10, 0, 2, 2), 8888) != 0) {
        printf("mynet: connect fail\n");
        close(Fd);
        return 1;
    }
    send(Fd, "hi\n", 3, 0);
    close(Fd);
    printf("mynet: ok\n");
    return 0;
}
