/*
 * Network.c — 开机模块表里的 Network。先网卡，再 UDP，再教学 TCP。
 */
#include "KernelModulesPrivate.h"
#include "HalDevices.h"
#include "Udp.h"
#include "Tcp.h"

int InitializeNetwork(void) {
    if (HalNetInitialize() != 0) {
        return -1;
    }
    UdpInitialize();
    TcpInitialize();
    return 0;
}
