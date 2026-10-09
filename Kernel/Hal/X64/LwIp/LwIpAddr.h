#ifndef LWIP_ADDR_H
#define LWIP_ADDR_H

#include "BootTypes.h"
#include "lwip/ip4_addr.h"

static inline void HostIpToLwIp(UINT32 HostStyle, ip4_addr_t *Out) {
    IP4_ADDR(Out,
             (u8_t)((HostStyle >> 24) & 0xFF),
             (u8_t)((HostStyle >> 16) & 0xFF),
             (u8_t)((HostStyle >> 8) & 0xFF),
             (u8_t)(HostStyle & 0xFF));
}

static inline UINT32 LwIpToHostIp(const ip4_addr_t *In) {
    return ((UINT32)ip4_addr1(In) << 24) |
           ((UINT32)ip4_addr2(In) << 16) |
           ((UINT32)ip4_addr3(In) << 8) |
           (UINT32)ip4_addr4(In);
}

#endif
