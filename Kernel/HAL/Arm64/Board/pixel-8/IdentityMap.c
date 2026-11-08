/*
 * IdentityMap.c — Pixel 8 BRINGUP 最小恒等页表
 *
 * UART @0x10800000 Device（GiB0 / L2）；内核 L1 1GiB @0x80000000；
 * USB-1：DWC3 @0x11200000 Device（同 GiB0 L2）。
 */
#include "Board.h"
#include "BoardConfig.h"
#include "HalSerial.h"

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

#define TABLE_FLAGS (DESC_VALID | DESC_TABLE)
#define BLOCK_NORMAL \
    (DESC_VALID | DESC_BLOCK | AF_BIT | SH_INNER | AP_EL1_RW | ATTR_NORMAL)
#define BLOCK_DEVICE \
    (DESC_VALID | DESC_BLOCK | AF_BIT | SH_INNER | AP_EL1_RW | UXN_BIT | ATTR_DEVICE)

static UINT64 sLevel0[512] __attribute__((aligned(4096)));
static UINT64 sLevel1[512] __attribute__((aligned(4096)));
static UINT64 sLevel2Low[512] __attribute__((aligned(4096)));

static void TableClear(UINT64 *Table) {
    UINTN Index;

    for (Index = 0; Index < 512u; Index++) {
        Table[Index] = 0;
    }
}

static void CleanDataCachePage(void *Page) {
    UINT64 Address = (UINT64)(UINTN)Page;
    UINT64 Offset;

    for (Offset = 0; Offset < 4096u; Offset += 64u) {
        __asm__ volatile("dc cvac, %0" ::"r"(Address + Offset) : "memory");
    }
    __asm__ volatile("dsb ish" ::: "memory");
}

static void ForceMmuAndCachesOff(void) {
    /* Linux INIT_SCTLR_EL1_MMU_OFF：勿 RMW 留 ABL 杂质 */
    UINT64 SystemControl = 0x30d00800ULL;

    __asm__ volatile("msr sctlr_el1, %0\nisb" ::"r"(SystemControl) : "memory");
    __asm__ volatile(
        "tlbi vmalle1\n"
        "dsb ish\n"
        "isb\n" ::: "memory");
}

static void WriteUartChar(char Value) {
    volatile UINT32 *Status = (volatile UINT32 *)(UINTN)0x10870010ULL;
    volatile UINT32 *Holding = (volatile UINT32 *)(UINTN)0x10870020ULL;
    UINT32 Guard = 0;

    while (((*Status) & (1u << 2)) == 0 && Guard < 200000u) {
        Guard++;
    }
    *Holding = (UINT32)(UINT8)Value;
    __asm__ volatile("dsb sy" ::: "memory");
}

int BoardBringUpIdentityMapEnable(void) {
    UINT64 Root;
    UINT64 Mair;
    UINT64 Tcr;
    UINT64 SystemControl;

    HalSerialWriteShell("mmu: prep\r\n");
    ForceMmuAndCachesOff();

    TableClear(sLevel0);
    TableClear(sLevel1);
    TableClear(sLevel2Low);

    sLevel0[0] = ((UINT64)(UINTN)sLevel1) | TABLE_FLAGS;
    /* GiB0：L2 表（UART）；GiB2：1GiB Normal 块（整窗 0x80000000） */
    sLevel1[0] = ((UINT64)(UINTN)sLevel2Low) | TABLE_FLAGS;
    sLevel1[2] = (0x80000000ULL) | BLOCK_NORMAL;

    sLevel2Low[0x84u] = (0x10800000ULL) | BLOCK_DEVICE;
#if defined(BOARD_PIXEL8_USB_HOST_BRINGUP) && BOARD_PIXEL8_USB_HOST_BRINGUP
    /* zuma snps,dwc3 @0x11210000 → 2MiB 块 0x11200000 */
    sLevel2Low[0x89u] = (0x11200000ULL) | BLOCK_DEVICE;
#endif

    HalSerialWriteShell("mmu: tables\r\n");
    CleanDataCachePage(sLevel0);
    CleanDataCachePage(sLevel1);
    CleanDataCachePage(sLevel2Low);

    Root = (UINT64)(UINTN)sLevel0;
    /* Attr0=Normal WB；Attr1=Device nGnRnE */
    Mair = (0xFFULL) | (0x00ULL << 8);
    /*
     * T0SZ=16；TG0=4K；Inner/Outer WB；Inner-shareable；IPS=40-bit；
     * EPD1=1：禁用 TTBR1，避免高半址乱走。
     */
    Tcr = (16ULL) | (0ULL << 14) | (1ULL << 8) | (1ULL << 10) | (3ULL << 12) |
          (1ULL << 23) | (2ULL << 32);

    __asm__ volatile("msr mair_el1, %0" ::"r"(Mair));
    __asm__ volatile("msr tcr_el1, %0" ::"r"(Tcr));
    __asm__ volatile("msr ttbr0_el1, %0" ::"r"(Root));
    __asm__ volatile(
        "dsb ish\n"
        "tlbi vmalle1\n"
        "dsb ish\n"
        "isb\n" ::: "memory");

    HalSerialWriteShell("mmu: ttbr\r\n");
    WriteUartChar('T');

    SystemControl = 0x30d00801ULL; /* MMU_OFF 模板 + M */
    __asm__ volatile(
        "msr sctlr_el1, %0\n"
        "isb\n"
        :
        : "r"(SystemControl)
        : "memory");

    /* 若能取指到此：开 M 成功；勿先调 C（防栈/PLT 变量） */
    WriteUartChar('M');

    HalSerialWriteShell("\r\nmmu: m\r\n");
    return 0;
}

#else

int BoardBringUpIdentityMapEnable(void) {
    return 0;
}

#endif
