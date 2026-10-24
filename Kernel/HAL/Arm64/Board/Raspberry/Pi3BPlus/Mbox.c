/*
 * Mbox.c — BCM283x VideoCore property mailbox
 *
 * 规格：BCM2835 ARM Peripherals mailbox；property channel 8。
 * GET_ARM_MEMORY（0x00010005）与 U-Boot dram_init 同源。
 * FB：先 GET 固件当前 physical（跟 U-Boot 同模），再 SET virt=phys 单缓冲、
 * y_offset=0，避免「桌面上半画在屏下半、再跳全屏」（双缓冲/错 offset）。
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
#define MBOX_TAG_GET_PHY_WH  0x00040003u
#define MBOX_TAG_SET_PHY_WH  0x00048003u
#define MBOX_TAG_GET_VIRT_WH 0x00040004u
#define MBOX_TAG_SET_VIRT_WH 0x00048004u
#define MBOX_TAG_SET_DEPTH   0x00048005u
#define MBOX_TAG_SET_PIXORD  0x00048006u
#define MBOX_TAG_GET_PITCH   0x00040008u
#define MBOX_TAG_GET_VOFF    0x00040009u
#define MBOX_TAG_SET_VOFF    0x00048009u

#define MBOX_PIXORD_RGB      1u

#define PHYS_TO_BUS(P)       (((UINT32)(P) & 0x3FFFFFFFu) | 0xC0000000u)
#define BUS_TO_PHYS(B)       ((UINT32)(B) & 0x3FFFFFFFu)

#define MBOX_SPIN_MAX        10000000u

#define FB_FALLBACK_W        1280u
#define FB_FALLBACK_H        720u
#define FB_REQ_BPP           32u
#define FB_ALIGN             4096u

static volatile UINT32 *MboxReg(UINT32 Off) {
    return (volatile UINT32 *)(UINTN)(MBOX_BASE + Off);
}

static void Dsb(void) {
    __asm__ volatile("dsb sy" ::: "memory");
}

static void BootHex32(UINT32 V) {
    char Buf[9];
    HalSerialFormatHex(Buf, V, 8);
    HalSerialBootMark(Buf);
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
    Buf[Words - 1] = 0;

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

int BoardFbSetup(BOOT_HANDOFF *Info) {
    /*
     * 一次 property：
     *  SET_PHY / SET_VIRT(=phy 单缓冲) / DEPTH / PIXORD / VOFF0 /
     *  ALLOC / GET_PITCH / GET_VIRT / GET_VOFF
     */
    static UINT32 W[48] __attribute__((aligned(16)));
    static UINT32 G[8] __attribute__((aligned(16)));
    UINT32 ReqW;
    UINT32 ReqH;
    UINT32 PhyW;
    UINT32 PhyH;
    UINT32 VirtW;
    UINT32 VirtH;
    UINT32 OffY;
    UINT32 Pitch;
    UINT32 FbBus;
    UINT32 FbSize;
    UINT32 FbPhys;
    UINT32 *Px;
    UINT32 i;

    if (Info == 0) {
        return -1;
    }

    /* 跟 U-Boot/固件当前输出，避免强改 1280x720 造成缩放/半屏窗 */
    ReqW = FB_FALLBACK_W;
    ReqH = FB_FALLBACK_H;
    G[2] = MBOX_TAG_GET_PHY_WH;
    G[3] = 8;
    G[4] = 0;
    G[5] = 0;
    G[6] = 0;
    if (MboxProp(G, 8) == 0 && G[5] != 0 && G[6] != 0) {
        ReqW = G[5];
        ReqH = G[6];
    }

    i = 2;
    W[i++] = MBOX_TAG_SET_PHY_WH;
    W[i++] = 8;
    W[i++] = 8;
    W[i++] = ReqW;
    W[i++] = ReqH;

    W[i++] = MBOX_TAG_SET_VIRT_WH;
    W[i++] = 8;
    W[i++] = 8;
    W[i++] = ReqW; /* 强制 virt == phys：禁 2× 双缓冲半屏 */
    W[i++] = ReqH;

    W[i++] = MBOX_TAG_SET_DEPTH;
    W[i++] = 4;
    W[i++] = 4;
    W[i++] = FB_REQ_BPP;

    W[i++] = MBOX_TAG_SET_PIXORD;
    W[i++] = 4;
    W[i++] = 4;
    W[i++] = MBOX_PIXORD_RGB;

    W[i++] = MBOX_TAG_SET_VOFF;
    W[i++] = 8;
    W[i++] = 8;
    W[i++] = 0;
    W[i++] = 0;

    W[i++] = MBOX_TAG_ALLOC_BUF;
    W[i++] = 8;
    W[i++] = 4;
    W[i++] = FB_ALIGN;
    W[i++] = 0;

    W[i++] = MBOX_TAG_GET_PITCH;
    W[i++] = 4;
    W[i++] = 0;
    W[i++] = 0;

    W[i++] = MBOX_TAG_GET_VIRT_WH;
    W[i++] = 8;
    W[i++] = 0;
    W[i++] = 0;
    W[i++] = 0;

    W[i++] = MBOX_TAG_GET_VOFF;
    W[i++] = 8;
    W[i++] = 0;
    W[i++] = 0;
    W[i++] = 0;

    if (MboxProp(W, i + 1) != 0) {
        return -1;
    }

    /* 按标签顺序回读（与上面写入序一致） */
    PhyW = W[5];
    PhyH = W[6];
    FbBus = W[28];
    FbSize = W[29];
    Pitch = W[33];
    VirtW = W[37];
    VirtH = W[38];
    OffY = W[43];

    if (PhyW == 0 || PhyH == 0 || FbBus == 0 || FbSize == 0 || Pitch < PhyW * 4u) {
        return -1;
    }

    /* 若固件仍给了双高 virt / 非零 y_offset，再钉一次 */
    if (VirtH > PhyH || OffY != 0 || VirtW != PhyW) {
        static UINT32 Fix[16] __attribute__((aligned(16)));
        Fix[2] = MBOX_TAG_SET_VIRT_WH;
        Fix[3] = 8;
        Fix[4] = 8;
        Fix[5] = PhyW;
        Fix[6] = PhyH;
        Fix[7] = MBOX_TAG_SET_VOFF;
        Fix[8] = 8;
        Fix[9] = 8;
        Fix[10] = 0;
        Fix[11] = 0;
        (void)MboxProp(Fix, 13);
        VirtW = PhyW;
        VirtH = PhyH;
        OffY = 0;
    }

    FbPhys = BUS_TO_PHYS(FbBus);
    Info->FrameBufferBase = (UINT64)FbPhys;
    /*
     * Handoff Size 用可见一屏，勿用 mailbox 可能偏大的整段（含双缓冲），
     * 否则后续 dc civac 可能扫进 0x3F000000 外设、砸串口 TX。
     */
    {
        UINT32 Vis = Pitch * PhyH;
        const UINT32 Peri = 0x3F000000u;

        if (Vis == 0 || Vis > FbSize) {
            Vis = FbSize;
        }
        if (FbPhys < Peri && FbPhys + Vis > Peri) {
            Vis = Peri - FbPhys;
        }
        Info->FrameBufferSize = (UINT64)Vis;
    }
    Info->HorizontalResolution = PhyW;
    Info->VerticalResolution = PhyH;
    Info->PixelsPerScanLine = Pitch / 4u;

    HalSerialBootMark("Boot: FB phy=");
    BootHex32(PhyW);
    HalSerialBootMark("x");
    BootHex32(PhyH);
    HalSerialBootMark(" virt=");
    BootHex32(VirtW);
    HalSerialBootMark("x");
    BootHex32(VirtH);
    HalSerialBootMark(" offy=");
    BootHex32(OffY);
    HalSerialBootMark(" pitch=");
    BootHex32(Pitch);
    HalSerialBootMark("\n");

    {
        UINT32 Vis = Pitch * PhyH;
        UINT32 N;
        UINT32 k;

        if (Vis > FbSize) {
            Vis = FbSize;
        }
        N = Vis / 4u;
        Px = (UINT32 *)(UINTN)FbPhys;
        for (k = 0; k < N; k++) {
            Px[k] = 0x00000000u;
        }
        HalDcacheCleanInvalidateRange(Px, Vis);
    }

    return 0;
}
