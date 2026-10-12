/*
 * Network.c — InitializeNetwork（开机模块编织）
 */
#include "KernelModulesPrivate.h"
#include "Hal.h"
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
