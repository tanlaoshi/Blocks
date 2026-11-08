/*
 * IdentityMap.c — MIX 2 BRINGUP 最小恒等页表
 *
 * GiB0：DWC3/QUSB/TCSR/TLMM(WEST) + SPMI arb（Type-C）Device；
 * GiB2：内核 Normal + splash Device。静态表；不碰 GIC / PhysicalMemory。
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

#define GIGABYTE2_BASE        0x80000000ULL
#define KERNEL_BLOCK_COUNT    8u
#define SPLASH_BLOCK_INDEX \
    ((UINTN)((BOARD_SPLASH_PHYSICAL_BASE - GIGABYTE2_BASE) >> 21))
#define SPLASH_BLOCK_COUNT \
    ((UINTN)(BOARD_SPLASH_PHYSICAL_SIZE >> 21))
/* DWC3@0xA800000 + QUSB@0xC012000 → 0xA000000..0xC3FFFFF */
#define USB_DEVICE_BLOCK_INDEX  0x50u
#define USB_DEVICE_BLOCK_COUNT  18u
/* tcsr_clamp_dig_n_1p8 @0x1FCB24C → 2MiB @0x1E00000 */
#define TCSR_CLAMP_BLOCK_INDEX  0x0Fu
/* TLMM WEST gpio59/60（WCD USBC analog）@0x353B000 → 2MiB @0x3400000 */
#define TLMM_WEST_BLOCK_INDEX   0x1Au
/* SPMI：core/cnfg @0x8000000；chnls @0x8400000（8×2MiB）；obsrvr 头 4MiB @0x9400000 */
#define SPMI_CORE_BLOCK_INDEX     0x40u
#define SPMI_CHANNELS_BLOCK_INDEX 0x42u
#define SPMI_CHANNELS_BLOCK_COUNT 8u
#define SPMI_OBSERVER_BLOCK_INDEX 0x4Au
#define SPMI_OBSERVER_BLOCK_COUNT 2u

static UINT64 sLevel0[512] __attribute__((aligned(4096)));
static UINT64 sLevel1[512] __attribute__((aligned(4096)));
static UINT64 sLevel2Gigabyte0[512] __attribute__((aligned(4096)));
static UINT64 sLevel2Gigabyte2[512] __attribute__((aligned(4096)));

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

int BoardBringUpIdentityMapEnable(void) {
    UINT64 Root;
    UINT64 Mair;
    UINT64 Tcr;
    UINT64 Sctlr;
    UINTN BlockIndex;

    TableClear(sLevel0);
    TableClear(sLevel1);
    TableClear(sLevel2Gigabyte0);
    TableClear(sLevel2Gigabyte2);

    sLevel0[0] = ((UINT64)(UINTN)sLevel1) | TABLE_FLAGS;
    sLevel1[0] = ((UINT64)(UINTN)sLevel2Gigabyte0) | TABLE_FLAGS;
    sLevel1[2] = ((UINT64)(UINTN)sLevel2Gigabyte2) | TABLE_FLAGS;

    for (BlockIndex = 0; BlockIndex < USB_DEVICE_BLOCK_COUNT; BlockIndex++) {
        UINTN TableIndex = USB_DEVICE_BLOCK_INDEX + BlockIndex;
        UINT64 Physical = ((UINT64)TableIndex) << 21;
        if (TableIndex >= 512u) {
            break;
        }
        sLevel2Gigabyte0[TableIndex] = Physical | BLOCK_DEVICE;
    }
    sLevel2Gigabyte0[TCSR_CLAMP_BLOCK_INDEX] =
        (((UINT64)TCSR_CLAMP_BLOCK_INDEX) << 21) | BLOCK_DEVICE;
    sLevel2Gigabyte0[TLMM_WEST_BLOCK_INDEX] =
        (((UINT64)TLMM_WEST_BLOCK_INDEX) << 21) | BLOCK_DEVICE;
    sLevel2Gigabyte0[SPMI_CORE_BLOCK_INDEX] =
        (((UINT64)SPMI_CORE_BLOCK_INDEX) << 21) | BLOCK_DEVICE;
    for (BlockIndex = 0; BlockIndex < SPMI_CHANNELS_BLOCK_COUNT; BlockIndex++) {
        UINTN TableIndex = SPMI_CHANNELS_BLOCK_INDEX + BlockIndex;
        sLevel2Gigabyte0[TableIndex] =
            (((UINT64)TableIndex) << 21) | BLOCK_DEVICE;
    }
    for (BlockIndex = 0; BlockIndex < SPMI_OBSERVER_BLOCK_COUNT; BlockIndex++) {
        UINTN TableIndex = SPMI_OBSERVER_BLOCK_INDEX + BlockIndex;
        sLevel2Gigabyte0[TableIndex] =
            (((UINT64)TableIndex) << 21) | BLOCK_DEVICE;
    }

    for (BlockIndex = 0; BlockIndex < KERNEL_BLOCK_COUNT; BlockIndex++) {
        UINT64 Physical = GIGABYTE2_BASE + (((UINT64)BlockIndex) << 21);
        sLevel2Gigabyte2[BlockIndex] = Physical | BLOCK_NORMAL;
    }

    for (BlockIndex = 0; BlockIndex < SPLASH_BLOCK_COUNT; BlockIndex++) {
        UINTN TableIndex = SPLASH_BLOCK_INDEX + BlockIndex;
        UINT64 Physical = BOARD_SPLASH_PHYSICAL_BASE + (((UINT64)BlockIndex) << 21);
        if (TableIndex >= 512u) {
            break;
        }
        sLevel2Gigabyte2[TableIndex] = Physical | BLOCK_DEVICE;
    }
#if defined(BOARD_SPLASH_PHYSICAL_BASE_ALT)
    {
        UINTN AltIndex =
            (UINTN)((BOARD_SPLASH_PHYSICAL_BASE_ALT - GIGABYTE2_BASE) >> 21);
        for (BlockIndex = 0; BlockIndex < SPLASH_BLOCK_COUNT; BlockIndex++) {
            UINTN TableIndex = AltIndex + BlockIndex;
            UINT64 Physical =
                BOARD_SPLASH_PHYSICAL_BASE_ALT + (((UINT64)BlockIndex) << 21);
            if (TableIndex >= 512u) {
                break;
            }
            sLevel2Gigabyte2[TableIndex] = Physical | BLOCK_DEVICE;
        }
    }
#endif

    CleanDataCachePage(sLevel0);
    CleanDataCachePage(sLevel1);
    CleanDataCachePage(sLevel2Gigabyte0);
    CleanDataCachePage(sLevel2Gigabyte2);

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
