/*
 * Mbox.c — BCM283x VideoCore property mailbox
 *
 * 规格：BCM2835 ARM Peripherals mailbox；property channel 8。
 * GET_ARM_MEMORY（0x00010005）与 U-Boot dram_init 同源。
 * FB：SET_PHYSICAL/VIRTUAL_W_H、SET_DEPTH、ALLOCATE_BUFFER、GET_PITCH
 * （BCM2835_MBOX_TAG_*，与 U-Boot mach/mbox.h 一致）。
 */
#include "Board.h"
#include "BootHandoff.h"
#include "BootTypes.h"
#include "Hal.h"

#define MBOX_BASE            0x3F00B880ULL
#define MBOX_RD              0x00
#define MBOX_STATUS0         0x18
#define MBOX_WR              0x20
#define MBOX_STATUS1         0x38

#define MBOX_FULL            0x80000000u
#define MBOX_EMPTY           0x40000000u
#define MBOX_CHAN_PROP       8u
#define MBOX_RESP_SUCCESS    0x80000000u

#define MBOX_TAG_GET_ARM_MEM 0x00010005u
#define MBOX_TAG_ALLOC_BUF   0x00040001u
#define MBOX_TAG_SET_PHY_WH  0x00048003u
#define MBOX_TAG_SET_VIRT_WH 0x00048004u
#define MBOX_TAG_SET_DEPTH   0x00048005u
#define MBOX_TAG_SET_PIXORD  0x00048006u
#define MBOX_TAG_GET_PITCH   0x00040008u
#define MBOX_TAG_SET_VOFF    0x00048009u

#define MBOX_PIXORD_RGB      1u

/* VC 看 DRAM：bus = phys | 0xC0000000（Pi2/3；≤1GiB） */
#define PHYS_TO_BUS(P)       (((UINT32)(P) & 0x3FFFFFFFu) | 0xC0000000u)
#define BUS_TO_PHYS(B)       ((UINT32)(B) & 0x3FFFFFFFu)

#define MBOX_SPIN_MAX        10000000u

/* 默认请求；VC 可在响应里改成实际值 */
#define FB_REQ_W             1280u
#define FB_REQ_H             720u
#define FB_REQ_BPP           32u
#define FB_ALIGN             4096u

static volatile UINT32 *MboxReg(UINT32 Off) {
    return (volatile UINT32 *)(UINTN)(MBOX_BASE + Off);
}

static void Dsb(void) {
    __asm__ volatile("dsb sy" ::: "memory");
}

/* 成功 0。Buf 须 16 字节对齐；Words = 字数（含 hdr/end）。 */
static int MboxProp(UINT32 *Buf, UINT32 Words) {
    UINT32 Bus;
    UINT32 Spin;
    UINT32 Val;

    if (Buf == 0 || Words < 3) {
        return -1;
    }

    Buf[0] = Words * 4u;
    Buf[1] = 0;
    Buf[Words - 1] = 0; /* end tag */

    HalDcacheCleanInvalidateRange(Buf, Words * 4u);
    Bus = PHYS_TO_BUS((UINTN)Buf);

    for (Spin = 0; Spin < MBOX_SPIN_MAX; Spin++) {
        if ((*MboxReg(MBOX_STATUS0) & MBOX_EMPTY) != 0) {
            break;
        }
        (void)*MboxReg(MBOX_RD);
    }
    if (Spin >= MBOX_SPIN_MAX) {
        return -1;
    }

    for (Spin = 0; Spin < MBOX_SPIN_MAX; Spin++) {
        if ((*MboxReg(MBOX_STATUS1) & MBOX_FULL) == 0) {
            break;
        }
    }
    if (Spin >= MBOX_SPIN_MAX) {
        return -1;
    }

    *MboxReg(MBOX_WR) = (Bus & ~0xFu) | MBOX_CHAN_PROP;
    Dsb();

    for (Spin = 0; Spin < MBOX_SPIN_MAX; Spin++) {
        if ((*MboxReg(MBOX_STATUS0) & MBOX_EMPTY) == 0) {
            Val = *MboxReg(MBOX_RD);
            if ((Val & 0xFu) == MBOX_CHAN_PROP) {
                break;
            }
        }
    }
    if (Spin >= MBOX_SPIN_MAX) {
        return -1;
    }

    HalDcacheCleanInvalidateRange(Buf, Words * 4u);
    if (Buf[1] != MBOX_RESP_SUCCESS) {
        return -1;
    }
    return 0;
}

int BoardDramProbe(UINT64 *Base, UINT64 *Size) {
    static UINT32 W[8] __attribute__((aligned(16)));
    UINT32 MemBase;
    UINT32 MemSize;

    if (Base == 0 || Size == 0) {
        return -1;
    }

    W[2] = MBOX_TAG_GET_ARM_MEM;
    W[3] = 8;
    W[4] = 0;
    W[5] = 0;
    W[6] = 0;

    if (MboxProp(W, 8) != 0) {
        return -1;
    }
    MemBase = W[5];
    MemSize = W[6];
    if (MemSize == 0) {
        return -1;
    }
    *Base = (UINT64)MemBase;
    *Size = (UINT64)MemSize;
    return 0;
}

/*
 * BoardFbSetup — mailbox 要帧缓冲并填 BOOT_HANDOFF。
 * 成功 0；失败 -1（保持串口壳，不复位）。
 */
int BoardFbSetup(BOOT_HANDOFF *Info) {
    /*
     * 布局（字）：
     *  0..1  hdr
     *  2..6  SET_PHY_WH
     *  7..11 SET_VIRT_WH
     * 12..15 SET_DEPTH
     * 16..19 SET_PIXORD
     * 20..24 SET_VOFF
     * 25..29 ALLOC_BUF
     * 30..33 GET_PITCH
     * 34     end
     */
    static UINT32 W[35] __attribute__((aligned(16)));
    UINT32 PhyW;
    UINT32 PhyH;
    UINT32 Pitch;
    UINT32 FbBus;
    UINT32 FbSize;
    UINT32 FbPhys;
    UINT32 *Px;

    if (Info == 0) {
        return -1;
    }

    W[2] = MBOX_TAG_SET_PHY_WH;
    W[3] = 8;
    W[4] = 8;
    W[5] = FB_REQ_W;
    W[6] = FB_REQ_H;

    W[7] = MBOX_TAG_SET_VIRT_WH;
    W[8] = 8;
    W[9] = 8;
    W[10] = FB_REQ_W;
    W[11] = FB_REQ_H;

    W[12] = MBOX_TAG_SET_DEPTH;
    W[13] = 4;
    W[14] = 4;
    W[15] = FB_REQ_BPP;

    W[16] = MBOX_TAG_SET_PIXORD;
    W[17] = 4;
    W[18] = 4;
    W[19] = MBOX_PIXORD_RGB;

    W[20] = MBOX_TAG_SET_VOFF;
    W[21] = 8;
    W[22] = 8;
    W[23] = 0;
    W[24] = 0;

    W[25] = MBOX_TAG_ALLOC_BUF;
    W[26] = 8;
    W[27] = 4;
    W[28] = FB_ALIGN; /* req alignment；resp → address */
    W[29] = 0;         /* resp size */

    W[30] = MBOX_TAG_GET_PITCH;
    W[31] = 4;
    W[32] = 0;
    W[33] = 0;

    if (MboxProp(W, 35) != 0) {
        return -1;
    }

    PhyW = W[5];
    PhyH = W[6];
    FbBus = W[28];
    FbSize = W[29];
    Pitch = W[33];

    if (PhyW == 0 || PhyH == 0 || FbBus == 0 || FbSize == 0 || Pitch < PhyW * 4u) {
        return -1;
    }

    FbPhys = BUS_TO_PHYS(FbBus);
    Info->FrameBufferBase = (UINT64)FbPhys;
    Info->FrameBufferSize = (UINT64)FbSize;
    Info->HorizontalResolution = PhyW;
    Info->VerticalResolution = PhyH;
    Info->PixelsPerScanLine = Pitch / 4u;

    /*
     * 按 pitch 铺深蓝（盖住 U-Boot Starting）。
     * 写后 HalDcacheCleanInvalidateRange：VC 与 CPU 不同 cache 域，
     * 只写 DRAM 缓存行而不 clean → 花屏（标准做法，非「务实省 flush」）。
     */
    {
        UINT32 PitchPx = Pitch / 4u;
        UINT32 Y;
        UINT32 X;
        volatile UINT32 *Row;

        Px = (UINT32 *)(UINTN)FbPhys;
        for (Y = 0; Y < PhyH; Y++) {
            Row = (volatile UINT32 *)(Px + (UINTN)Y * PitchPx);
            for (X = 0; X < PhyW; X++) {
                Row[X] = 0x00001A33u;
            }
        }
        HalDcacheCleanInvalidateRange(Px, FbSize);
    }

    return 0;
}
