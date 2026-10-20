/*
 * Network.c — 模块表 Network（Full / VirtDesktop；Virt 串口表无此项）
 *
 * NetworkInitialize（x86 软成功：HalNetInitialize→NetInitialize 恒 0，!=0 分支走不到）：
 *   HalNetInitialize → NetInitialize（ConfigEnsure + Probe NET；失败仍 0）
 *   → UdpInitialize（教学 UDP 队列，非 lwIP）
 *   → TcpInitialize（教学单连接；lwIP 另懒加载）
 * 不开 lwip_init。HAL 探测入口 NetInitialize ≠ 本模块 NetworkInitialize。
 */
#include "KernelModulesPrivate.h"
#include "HalDevices.h"
#include "Udp.h"
#include "Tcp.h"

int NetworkInitialize(void) {
    if (HalNetInitialize() != 0) {
        return -1;
    }
    UdpInitialize();
    TcpInitialize();
    return 0;
}
