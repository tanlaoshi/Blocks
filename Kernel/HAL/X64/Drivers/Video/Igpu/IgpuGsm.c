/*
 * IgpuGsm.c — PR-G-igpu-3：Gen8+ GSM（GGTT PTE 表在 BAR 后半）
 *
 * 对照 i915：GSM = GTTMMADR + size/2；PTE = phys | PRESENT；GRAPHICS_FLSH 作废。
 * 禁止改 GTT 偏移 0（固件 scanout）。
 */
#include "Igpu.h"
#include "SerialLog.h"

#define IGPU_GSM_OFF           (8u * 1024u * 1024u)
#define IGPU_GEN8_PTE_PRESENT  1ull
#define IGPU_REG_GRAPHICS_FLSH      0x101008u
#define IGPU_GRAPHICS_FLSH_EN       1u

static volatile UINT64 *gIgpuGsm;
static UINTN gIgpuGsmEntries;
static int gIgpuGsmOk;

int IgpuGsmOk(void) {
    return gIgpuGsmOk;
}

static void FlushGtt(void) {
    IgpuMmioWrite32(IGPU_REG_GRAPHICS_FLSH, IGPU_GRAPHICS_FLSH_EN);
    (void)IgpuMmioRead32(IGPU_REG_GRAPHICS_FLSH);
}

UINT64 IgpuGsmPteRead(UINT64 GttOff) {
    UINTN Idx;

    if (!gIgpuGsmOk) {
        return 0;
    }
    Idx = (UINTN)(GttOff >> 12);
    if (Idx >= gIgpuGsmEntries) {
        return 0;
    }
    return gIgpuGsm[Idx];
}

int IgpuGsmMapQuiet(UINT64 GttOff, UINT64 Phys) {
    UINTN Idx;

    if (!gIgpuGsmOk) {
        return 0;
    }
    if (GttOff == 0) {
        return 0; /* 保护 scanout */
    }
    if ((GttOff & 0xFFFu) != 0 || (Phys & 0xFFFu) != 0) {
        return 0;
    }
    Idx = (UINTN)(GttOff >> 12);
    if (Idx >= gIgpuGsmEntries) {
        return 0;
    }
    /* 分两次 32 位写，避免部分机对 GSM 的 64 位写丢弃 */
    {
        volatile UINT32 *Pw = (volatile UINT32 *)&gIgpuGsm[Idx];
        UINT64 Pte = (Phys & ~0xFFFull) | IGPU_GEN8_PTE_PRESENT;
        Pw[0] = (UINT32)Pte;
        Pw[1] = (UINT32)(Pte >> 32);
    }
    __asm__ volatile ("mfence" ::: "memory");
    return 1;
}

void IgpuGsmFlush(void) {
    if (!gIgpuGsmOk) {
        return;
    }
    FlushGtt();
}

int IgpuGsmMap(UINT64 GttOff, UINT64 Phys) {
    if (!IgpuGsmMapQuiet(GttOff, Phys)) {
        return 0;
    }
    IgpuGsmFlush();
    return 1;
}

int IgpuGsmInit(void) {
    UINT64 Pte0;
    UINTN Map;

    if (gIgpuGsmOk) {
        return 1;
    }
    if (!IgpuMmioOk()) {
        return 0;
    }
    Map = IgpuMmioMapBytes();
    if (Map < IGPU_GSM_OFF + 0x1000u) {
        LogBoot("Boot: igpu gsm soft (map)\n");
        return 0;
    }

    gIgpuGsm = (volatile UINT64 *)(UINTN)(IgpuMmioBase() + IGPU_GSM_OFF);
    gIgpuGsmEntries = (Map - IGPU_GSM_OFF) / sizeof(UINT64);
    gIgpuGsmOk = 1;

    Pte0 = IgpuGsmPteRead(0);
    LogBoot("Boot: igpu gsm ok pte0=");
    LogBootHex32((UINT32)(Pte0 >> 32));
    LogBoot(":");
    LogBootHex32((UINT32)Pte0);
    LogBoot("\n");
    return 1;
}
