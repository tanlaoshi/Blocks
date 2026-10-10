/*
 * HalVirtioNet.c — X64 legacy virtio-net（K20）
 *
 * 【初学者】
 * PCI 1af4:1000 → IO BAR → RX q0 + TX q1。
 * 帧前加 10 字节 virtio_net_hdr。Runtime 须 disable-modern=on。
 */
#include "HalNet.h"
#include "HalDma.h"

#define VIRTIO_VID     0x1AF4u
#define VIRTIO_NET_DID 0x1000u

#define VIRTIO_PCI_HOST_F   0x00u
#define VIRTIO_PCI_GUEST_F  0x04u
#define VIRTIO_PCI_Q_PFN    0x08u
#define VIRTIO_PCI_Q_NUM    0x0Cu
#define VIRTIO_PCI_Q_SEL    0x0Eu
#define VIRTIO_PCI_Q_NOTIFY 0x10u
#define VIRTIO_PCI_STATUS   0x12u
#define VIRTIO_PCI_ISR      0x13u
#define VIRTIO_PCI_CONFIG   0x14u

#define VIRTIO_ACK       1u
#define VIRTIO_DRIVER    2u
#define VIRTIO_DRIVER_OK 4u
#define VIRTIO_NET_F_MAC (1u << 5)

#define VRING_DESC_F_NEXT  1u
#define VRING_DESC_F_WRITE 2u

#define VRING_ALIGN 4096u
#define QMAX        256u
#define NET_HDR_LEN 10u
#define FRAME_MAX   1518u

typedef struct {
    UINT64 Addr;
    UINT32 Len;
    UINT16 Flags;
    UINT16 Next;
} VqDesc;

typedef struct {
    VqDesc *Desc;
    UINT16 *AvailIdx;
    UINT16 *AvailRing;
    UINT16 *UsedIdx;
    UINT16 LastUsed;
    UINT16 Qsz;
    UINT64 Phys;
} Vq;

static UINT16 gIo;
static int gReady;
static UINT8 gMac[6];
static Vq gRx;
static Vq gTx;
static UINT8 *gTxDma; /* hdr + frame */
static UINT8 *gRxDma; /* hdr + frame */

static void Out8(UINT16 Port, UINT8 V) {
    __asm__ volatile("outb %0, %1" : : "a"(V), "Nd"(Port));
}
static void Out16(UINT16 Port, UINT16 V) {
    __asm__ volatile("outw %0, %1" : : "a"(V), "Nd"(Port));
}
static void Out32(UINT16 Port, UINT32 V) {
    __asm__ volatile("outl %0, %1" : : "a"(V), "Nd"(Port));
}
static UINT8 In8(UINT16 Port) {
    UINT8 V;
    __asm__ volatile("inb %1, %0" : "=a"(V) : "Nd"(Port));
    return V;
}
static UINT16 In16(UINT16 Port) {
    UINT16 V;
    __asm__ volatile("inw %1, %0" : "=a"(V) : "Nd"(Port));
    return V;
}
static UINT32 In32(UINT16 Port) {
    UINT32 V;
    __asm__ volatile("inl %1, %0" : "=a"(V) : "Nd"(Port));
    return V;
}

static void PciWrite32(UINT8 Bus, UINT8 Dev, UINT8 Func, UINT8 Off, UINT32 Val) {
    UINT32 Addr = 0x80000000u | ((UINT32)Bus << 16) | ((UINT32)(Dev & 31) << 11) |
                  ((UINT32)(Func & 7) << 8) | (Off & 0xFCu);
    Out32(0xCF8u, Addr);
    Out32(0xCFCu, Val);
}

static UINT32 PciRead32(UINT8 Bus, UINT8 Dev, UINT8 Func, UINT8 Off) {
    UINT32 Addr = 0x80000000u | ((UINT32)Bus << 16) | ((UINT32)(Dev & 31) << 11) |
                  ((UINT32)(Func & 7) << 8) | (Off & 0xFCu);
    Out32(0xCF8u, Addr);
    return In32(0xCFCu);
}

static void Zero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    UINTN i;
    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}

static UINT32 AlignUp(UINT32 V, UINT32 A) {
    return (V + A - 1u) & ~(A - 1u);
}

static UINT32 VringBytes(UINT16 Num) {
    UINT32 Desc = 16u * (UINT32)Num;
    UINT32 Avail = 6u + 2u * (UINT32)Num;
    UINT32 UsedOff = AlignUp(Desc + Avail, VRING_ALIGN);
    return UsedOff + 6u + 8u * (UINT32)Num;
}

static int FindVirtioNet(UINT16 *IoBar) {
    UINT16 b;
    UINT8 d, f;
    for (b = 0; b < 8; b++) {
        for (d = 0; d < 32; d++) {
            for (f = 0; f < 8; f++) {
                UINT32 Id = PciRead32((UINT8)b, d, f, 0);
                UINT16 Vid = (UINT16)(Id & 0xFFFF);
                UINT16 Did = (UINT16)(Id >> 16);
                UINT32 Bar;
                UINT16 Cmd;
                if (Vid == 0xFFFF) {
                    if (f == 0) {
                        break;
                    }
                    continue;
                }
                if (Vid != VIRTIO_VID || Did != VIRTIO_NET_DID) {
                    continue;
                }
                Bar = PciRead32((UINT8)b, d, f, 0x10);
                if ((Bar & 1u) == 0) {
                    return -1;
                }
                Cmd = (UINT16)(PciRead32((UINT8)b, d, f, 0x04) & 0xFFFF);
                Cmd |= 0x5u;
                PciWrite32((UINT8)b, d, f, 0x04,
                           (PciRead32((UINT8)b, d, f, 0x04) & 0xFFFF0000u) | Cmd);
                *IoBar = (UINT16)(Bar & ~3u);
                return 0;
            }
        }
    }
    return -1;
}

static int VqSetup(Vq *Q, UINT16 Sel) {
    UINT16 Qnum;
    UINT32 Bytes;
    UINT32 PagesN;
    void *Pages;
    UINT32 DescBytes;
    UINT32 AvailOff;
    UINT32 UsedOff;

    Out16(gIo + VIRTIO_PCI_Q_SEL, Sel);
    Qnum = In16(gIo + VIRTIO_PCI_Q_NUM);
    if (Qnum < 4u || Qnum > QMAX) {
        return -1;
    }
    Q->Qsz = Qnum;
    Bytes = VringBytes(Q->Qsz);
    PagesN = (Bytes + 4095u) / 4096u;
    Pages = HalDmaAllocatePages(PagesN);
    if (Pages == 0) {
        return -1;
    }
    Zero(Pages, (UINTN)PagesN * 4096u);
    Q->Phys = (UINT64)(UINTN)Pages;
    DescBytes = 16u * (UINT32)Q->Qsz;
    AvailOff = DescBytes;
    UsedOff = AlignUp(DescBytes + 6u + 2u * (UINT32)Q->Qsz, VRING_ALIGN);
    Q->Desc = (VqDesc *)Pages;
    Q->AvailIdx = (UINT16 *)((UINT8 *)Pages + AvailOff + 2u);
    Q->AvailRing = (UINT16 *)((UINT8 *)Pages + AvailOff + 4u);
    Q->UsedIdx = (UINT16 *)((UINT8 *)Pages + UsedOff + 2u);
    Q->LastUsed = 0;
    Out16(gIo + VIRTIO_PCI_Q_SEL, Sel);
    Out32(gIo + VIRTIO_PCI_Q_PFN, (UINT32)(Q->Phys >> 12));
    return 0;
}

static void RxPost(void) {
    UINT16 Aidx;
    Zero(gRxDma, NET_HDR_LEN + FRAME_MAX);
    gRx.Desc[0].Addr = (UINT64)(UINTN)gRxDma;
    gRx.Desc[0].Len = NET_HDR_LEN + FRAME_MAX;
    gRx.Desc[0].Flags = VRING_DESC_F_WRITE;
    gRx.Desc[0].Next = 0;
    Aidx = *gRx.AvailIdx;
    gRx.AvailRing[Aidx % gRx.Qsz] = 0;
    __asm__ volatile("mfence" ::: "memory");
    *gRx.AvailIdx = (UINT16)(Aidx + 1);
    __asm__ volatile("mfence" ::: "memory");
    Out16(gIo + VIRTIO_PCI_Q_NOTIFY, 0);
}

int HalNetInitialize(void) {
    UINT16 Io;
    UINT32 HostF;
    UINT32 GuestF;
    void *Dma;
    UINTN i;

    gReady = 0;
    if (FindVirtioNet(&Io) != 0) {
        return -1;
    }
    gIo = Io;

    Out8(gIo + VIRTIO_PCI_STATUS, 0);
    Out8(gIo + VIRTIO_PCI_STATUS, (UINT8)(VIRTIO_ACK | VIRTIO_DRIVER));
    HostF = In32(gIo + VIRTIO_PCI_HOST_F);
    GuestF = 0;
    if (HostF & VIRTIO_NET_F_MAC) {
        GuestF |= VIRTIO_NET_F_MAC;
    }
    Out32(gIo + VIRTIO_PCI_GUEST_F, GuestF);

    if (GuestF & VIRTIO_NET_F_MAC) {
        for (i = 0; i < 6; i++) {
            gMac[i] = In8((UINT16)(gIo + VIRTIO_PCI_CONFIG + (UINT16)i));
        }
    } else {
        gMac[0] = 0x52;
        gMac[1] = 0x54;
        gMac[2] = 0x00;
        gMac[3] = 0x12;
        gMac[4] = 0x34;
        gMac[5] = 0x56;
    }

    if (VqSetup(&gRx, 0) != 0 || VqSetup(&gTx, 1) != 0) {
        return -1;
    }

    Dma = HalDmaAllocatePages(2);
    if (Dma == 0) {
        return -1;
    }
    Zero(Dma, 8192u);
    gTxDma = (UINT8 *)Dma;
    gRxDma = (UINT8 *)Dma + 4096u;

    Out8(gIo + VIRTIO_PCI_STATUS, (UINT8)(VIRTIO_ACK | VIRTIO_DRIVER | VIRTIO_DRIVER_OK));
    RxPost();
    gReady = 1;
    (void)In8(gIo + VIRTIO_PCI_ISR);
    return 0;
}

int HalNetReady(void) {
    return gReady;
}

void HalNetGetMac(UINT8 Mac[6]) {
    UINTN i;
    for (i = 0; i < 6; i++) {
        Mac[i] = gMac[i];
    }
}

int HalNetTransmit(const void *Frame, UINT32 Len) {
    UINT16 Aidx;
    UINT32 Spin;
    UINT16 Uidx;
    UINTN i;
    const UINT8 *Src;

    if (!gReady || Frame == 0 || Len == 0 || Len > FRAME_MAX) {
        return -1;
    }
    Zero(gTxDma, NET_HDR_LEN);
    Src = (const UINT8 *)Frame;
    for (i = 0; i < Len; i++) {
        gTxDma[NET_HDR_LEN + i] = Src[i];
    }

    gTx.Desc[0].Addr = (UINT64)(UINTN)gTxDma;
    gTx.Desc[0].Len = NET_HDR_LEN + Len;
    gTx.Desc[0].Flags = 0;
    gTx.Desc[0].Next = 0;

    Aidx = *gTx.AvailIdx;
    gTx.AvailRing[Aidx % gTx.Qsz] = 0;
    __asm__ volatile("mfence" ::: "memory");
    *gTx.AvailIdx = (UINT16)(Aidx + 1);
    __asm__ volatile("mfence" ::: "memory");
    Out16(gIo + VIRTIO_PCI_Q_NOTIFY, 1);

    for (Spin = 0; Spin < 4000000u; Spin++) {
        __asm__ volatile("mfence" ::: "memory");
        Uidx = *gTx.UsedIdx;
        if (Uidx != gTx.LastUsed) {
            gTx.LastUsed = Uidx;
            return 0;
        }
        __asm__ volatile("pause");
    }
    return -1;
}

int HalNetReceive(void *Buf, UINT32 Cap) {
    UINT16 Uidx;
    UINT32 Len;
    UINT32 Slot;
    UINTN i;
    UINT8 *Dst;
    UINT32 *UsedElem; /* id, len */

    if (!gReady || Buf == 0 || Cap == 0) {
        return -1;
    }
    __asm__ volatile("mfence" ::: "memory");
    Uidx = *gRx.UsedIdx;
    if (Uidx == gRx.LastUsed) {
        return 0;
    }
    Slot = (UINT32)((Uidx - 1u) % gRx.Qsz);
    UsedElem = (UINT32 *)((UINT8 *)gRx.UsedIdx + 2u + Slot * 8u);
    Len = UsedElem[1];
    gRx.LastUsed = Uidx;
    if (Len <= NET_HDR_LEN) {
        RxPost();
        return 0;
    }
    Len -= NET_HDR_LEN;
    if (Len > Cap) {
        Len = Cap;
    }
    if (Len > FRAME_MAX) {
        Len = FRAME_MAX;
    }
    Dst = (UINT8 *)Buf;
    for (i = 0; i < Len; i++) {
        Dst[i] = gRxDma[NET_HDR_LEN + i];
    }
    RxPost();
    return (int)Len;
}
