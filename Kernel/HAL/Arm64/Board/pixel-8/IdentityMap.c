/*
 * IdentityMap.c — Pixel 8 BRINGUP 最小恒等页表
 *
 * 映射：UART 窗 @0x10800000 Device（GiB0）+ 内核窗 @0x90600000 Normal（GiB2）。
 * 静态表，不碰 PhysicalMemory / GIC。对表 ARM ARM EL1 TTBR0 4K / 2MiB block。
 */
#include "Board.h"
#include "BoardConfig.h"

#ifndef BOARD_BRINGUP_IDENTITY_MAP
#define BOARD_BRINGUP_IDENTITY_MAP 0
#endif

#if BOARD_BRINGUP_IDENTITY_MAP

#define DESC_VALID   (1ULL << 0)
#define DESC_TABLE   (1ULL << 1)
#define DESC_BLOCK   (0ULL << 1)
#define AF_BIT       (1ULL << 10)
#define UXN_BIT      (1ULL << 54)
#define SH_INNER     (3ULL << 8)
#define AP_EL1_RW    (0ULL << 6)
#define ATTR_NORMAL  (0ULL << 2)
#define ATTR_DEVICE  (1ULL << 2)

#define TABLE_FLAGS (DESC_VALID | DESC_TABLE | AF_BIT | ATTR_NORMAL | SH_INNER | AP_EL1_RW)
#define BLOCK_NORMAL \
    (DESC_VALID | DESC_BLOCK | AF_BIT | SH_INNER | AP_EL1_RW | UXN_BIT | ATTR_NORMAL)
#define BLOCK_DEVICE \
    (DESC_VALID | DESC_BLOCK | AF_BIT | SH_INNER | AP_EL1_RW | UXN_BIT | ATTR_DEVICE)

static UINT64 sLevel0[512] __attribute__((aligned(4096)));
static UINT64 sLevel1[512] __attribute__((aligned(4096)));
static UINT64 sLevel2Low[512] __attribute__((aligned(4096)));
static UINT64 sLevel2High[512] __attribute__((aligned(4096)));

static void TableClear(UINT64 *Table) {
    UINTN Index;

    for (Index = 0; Index < 512u; Index++) {
        Table[Index] = 0;
    }
}

/* MMU off 时写表在 cache；开 M+C 前须 clean 到 PoC，否则 walk 读到旧零页 */
static void CleanDataCachePage(void *Page) {
    UINT64 Address = (UINT64)(UINTN)Page;
    UINT64 Offset;

    for (Offset = 0; Offset < 4096u; Offset += 64u) {
        __asm__ volatile("dc cvac, %0" ::"r"(Address + Offset) : "memory");
    }
    __asm__ volatile("dsb ish" ::: "memory");
}

int BoardBringUpIdentityMapEnable(void) {
    UINT64 Root;
    UINT64 Mair;
    UINT64 Tcr;
    UINT64 Sctlr;
    UINTN BlockIndex;

    TableClear(sLevel0);
    TableClear(sLevel1);
    TableClear(sLevel2Low);
    TableClear(sLevel2High);

    sLevel0[0] = ((UINT64)(UINTN)sLevel1) | TABLE_FLAGS;
    /* GiB0：UART；GiB2：内核 @0x90701000 */
    sLevel1[0] = ((UINT64)(UINTN)sLevel2Low) | TABLE_FLAGS;
    sLevel1[2] = ((UINT64)(UINTN)sLevel2High) | TABLE_FLAGS;

    sLevel2Low[0x84u] = (0x10800000ULL) | BLOCK_DEVICE;

    /* 0x90600000..0x91000000：5×2MiB Normal（含 Image + BSS + 栈） */
    for (BlockIndex = 0x83u; BlockIndex < 0x88u; BlockIndex++) {
        UINT64 Physical = (2ULL << 30) + (((UINT64)BlockIndex) << 21);
        sLevel2High[BlockIndex] = Physical | BLOCK_NORMAL;
    }

    CleanDataCachePage(sLevel0);
    CleanDataCachePage(sLevel1);
    CleanDataCachePage(sLevel2Low);
    CleanDataCachePage(sLevel2High);

    Root = (UINT64)(UINTN)sLevel0;
    Mair = (0xFFULL) | (0x00ULL << 8);
    Tcr = (16ULL) | (0ULL << 14) | (1ULL << 8) | (1ULL << 10) | (3ULL << 12) | (2ULL << 32);

    __asm__ volatile("msr mair_el1, %0" ::"r"(Mair));
    __asm__ volatile("msr tcr_el1, %0" ::"r"(Tcr));
    __asm__ volatile("msr ttbr0_el1, %0" ::"r"(Root));
    __asm__ volatile(
        "dsb ish\n"
        "tlbi vmalle1\n"
        "dsb ish\n"
        "isb\n" ::: "memory");

    /*
     * 先只开 M+I（不开 C）：表已 clean，取指走 I；UART Device 不依赖 D-cache。
     * 避免脏 D$ 与 walk 打架。
     */
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(Sctlr));
    Sctlr |= (1ULL << 0) | (1ULL << 12);
    Sctlr &= ~((1ULL << 2) | (1ULL << 19));
    __asm__ volatile("msr sctlr_el1, %0\nisb\n" ::"r"(Sctlr) : "memory");
    return 0;
}

#else

int BoardBringUpIdentityMapEnable(void) {
    return 0;
}

#endif
