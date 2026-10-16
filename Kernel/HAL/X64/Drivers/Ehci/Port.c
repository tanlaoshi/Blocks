/*
 * Port.c — 根口复位（PR-H-ehci-2）
 */
#include "Private.h"
#include "Hal.h"
#include "ToySerialLog.h"

int PortReset(EHCI_CTRL *C, UINT8 Port) {
    UINT32 Ps;
    int Spin;

    if (!C || Port == 0 || Port > C->NPorts) {
        return 0;
    }
    Ps = R32(C->Op, EHCI_PORTSC(Port));
    if (Ps & EHCI_PORT_OWNER) {
        gEhciLastErr = "port companion";
        return 0;
    }

    Ps = R32(C->Op, EHCI_PORTSC(Port));
    Ps &= ~(EHCI_PORT_CSC | EHCI_PORT_PEDC | EHCI_PORT_OCC);
    Ps |= EHCI_PORT_PP | EHCI_PORT_PR;
    W32(C->Op, EHCI_PORTSC(Port), Ps);
    Delay(1000000);

    Ps = R32(C->Op, EHCI_PORTSC(Port));
    Ps &= ~(EHCI_PORT_PR | EHCI_PORT_CSC | EHCI_PORT_PEDC | EHCI_PORT_OCC);
    Ps |= EHCI_PORT_PP;
    W32(C->Op, EHCI_PORTSC(Port), Ps);

    Spin = 1000000;
    while (Spin-- > 0) {
        Ps = R32(C->Op, EHCI_PORTSC(Port));
        if ((Ps & EHCI_PORT_PR) == 0) {
            break;
        }
        HalCpuRelax();
    }
    Delay(1000000);

    Ps = R32(C->Op, EHCI_PORTSC(Port));
    if (Ps & EHCI_PORT_OWNER) {
        gEhciLastErr = "port companion";
        ToyBootMarkUsb("Boot: EHCI port → companion (FS; need UHCI)\n");
        return 0;
    }
    if ((Ps & EHCI_PORT_PED) == 0) {
        /* 再 PR 一次（ehci hid 热重试常要） */
        Ps &= ~(EHCI_PORT_CSC | EHCI_PORT_PEDC | EHCI_PORT_OCC);
        Ps |= EHCI_PORT_PP | EHCI_PORT_PR;
        W32(C->Op, EHCI_PORTSC(Port), Ps);
        Delay(1000000);
        Ps = R32(C->Op, EHCI_PORTSC(Port));
        Ps &= ~(EHCI_PORT_PR | EHCI_PORT_CSC | EHCI_PORT_PEDC | EHCI_PORT_OCC);
        Ps |= EHCI_PORT_PP;
        W32(C->Op, EHCI_PORTSC(Port), Ps);
        Spin = 1000000;
        while (Spin-- > 0) {
            Ps = R32(C->Op, EHCI_PORTSC(Port));
            if ((Ps & EHCI_PORT_PR) == 0) {
                break;
            }
            HalCpuRelax();
        }
        Delay(1000000);
        Ps = R32(C->Op, EHCI_PORTSC(Port));
    }
    if ((Ps & EHCI_PORT_PED) == 0) {
        gEhciLastErr = "port no PED";
        return 0;
    }
    if ((Ps & EHCI_PORT_CCS) == 0) {
        gEhciLastErr = "port lost CCS";
        return 0;
    }
    return 1;
}
