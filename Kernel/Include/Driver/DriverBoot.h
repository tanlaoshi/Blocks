/*
 * DriverBoot.h — 驱动自己挂上启动表。HalDriverRegister 按 Order 调用。
 * 新驱动在自己的 .c 里加一行 DRIVER_BOOT，不必改 HalDevices.c。
 */
#ifndef DRIVER_BOOT_H
#define DRIVER_BOOT_H

#include "Compiler.h"

enum {
    DRIVER_BOOT_AHCI = 10,
    DRIVER_BOOT_ATA = 20,
    DRIVER_BOOT_NVME = 30,
    DRIVER_BOOT_USB_HOST = 40,
    DRIVER_BOOT_MSC_HOST = 50,
    DRIVER_BOOT_MSC = 60,
    DRIVER_BOOT_INPUT_XHCI = 70,
    DRIVER_BOOT_INPUT_EHCI = 80,
    DRIVER_BOOT_INPUT_UHCI = 90,
    DRIVER_BOOT_INPUT_PS2 = 100,
    DRIVER_BOOT_NET = 110,
    DRIVER_BOOT_E1000 = 120,
    DRIVER_BOOT_ALX = 130,
    DRIVER_BOOT_RTL8168 = 140,
    DRIVER_BOOT_WIFI_XHCI = 150,
    DRIVER_BOOT_WIFI_EHCI = 160,
    DRIVER_BOOT_RTL8188EU = 170,
    DRIVER_BOOT_IWL = 180,
    DRIVER_BOOT_IGPU = 190,
    DRIVER_BOOT_HDA = 200,
    DRIVER_BOOT_DEMO = 210
};

typedef struct {
    int Order;
    void (*Call)(void);
} DRIVER_BOOT;

#define DRIVER_BOOT(Order, Call) \
    static const DRIVER_BOOT Call##_Boot \
        BLOCKS_USED BLOCKS_SECTION(".toy.driver.boot") BLOCKS_ALIGNED(16) = { \
            (Order), (Call) \
        }

#endif
