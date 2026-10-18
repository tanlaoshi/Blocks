/*
 * NetworkInit.c — InitializeNetwork（开机模块表；Services/Tcp 本层）
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
