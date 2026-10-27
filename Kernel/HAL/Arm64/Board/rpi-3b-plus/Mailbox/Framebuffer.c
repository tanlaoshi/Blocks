/*
 * Framebuffer.c — VC mailbox 帧缓冲 Setup（bd-pi-sd）
 */
#include "Mailbox/Private.h"

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
    if (MailboxProperty(G, 8) == 0 && G[5] != 0 && G[6] != 0) {
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

    /* 固件常忽略/保持 RGB；真纠正在 HalVideoSetFbSwapRb（见下） */
    W[i++] = MBOX_TAG_SET_PIXORD;
    W[i++] = 4;
    W[i++] = 4;
    W[i++] = MBOX_PIXORD_BGR;

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

    if (MailboxProperty(W, i + 1) != 0) {
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
        (void)MailboxProperty(Fix, 13);
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
    MailboxBootHexadecimal32(PhyW);
    HalSerialBootMark("x");
    MailboxBootHexadecimal32(PhyH);
    HalSerialBootMark(" virt=");
    MailboxBootHexadecimal32(VirtW);
    HalSerialBootMark("x");
    MailboxBootHexadecimal32(VirtH);
    HalSerialBootMark(" offy=");
    MailboxBootHexadecimal32(OffY);
    HalSerialBootMark(" pitch=");
    MailboxBootHexadecimal32(Pitch);
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
