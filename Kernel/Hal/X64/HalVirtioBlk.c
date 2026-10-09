/*
 * HalVirtioBlk.c — X64 legacy virtio-blk 读写扇区（K16 读 / K24 写）
 *
 * 【初学者】
 * PCI 找 1af4:1001 → IO BAR → 按设备 QUEUE_NUM 建 vring → READ/WRITE。
 * Runtime 须 disable-modern=on，走 legacy 口。
 */
#include "HalBlock.h"
#include "HalDma.h"

#define VIRTIO_VID          0x1AF4u
#define VIRTIO_BLK_DID      0x1001u

#define VIRTIO_PCI_HOST_F   0x00u
#define VIRTIO_PCI_GUEST_F  0x04u
#define VIRTIO_PCI_Q_PFN    0x08u
#define VIRTIO_PCI_Q_NUM    0x0Cu
#define VIRTIO_PCI_Q_SEL    0x0Eu
#define VIRTIO_PCI_Q_NOTIFY 0x10u
#define VIRTIO_PCI_STATUS   0x12u

#define VIRTIO_ACK          1u
#define VIRTIO_DRIVER       2u
#define VIRTIO_DRIVER_OK    4u

#define VRING_DESC_F_NEXT   1u
#define VRING_DESC_F_WRITE  2u
#define VIRTIO_BLK_T_IN     0u
#define VIRTIO_BLK_T_OUT    1u

#define SECTOR_SIZE         512u
#define VRING_ALIGN         4096u
#define QMAX                256u

typedef struct {
    UINT64 Addr;
    UINT32 Len;
    UINT16 Flags;
    UINT16 Next;
} VqDesc;

typedef struct {
    UINT32 Type;
    UINT32 Reserved;
    UINT64 Sector;
} BlkReq;

static UINT16 gIo;
static int gReady;
static UINT16 gQsz;
static VqDesc *gDesc;
static UINT16 *gAvailIdx;
static UINT16 *gAvailRing;
static UINT16 *gUsedIdx;
static UINT16 gLastUsed;
static BlkReq *gDmaReq;
static UINT8 *gDmaStatus;
static UINT8 *gDmaSec;

static void Out8(UINT16 Port, UINT8 V) {
    __asm__ volatile("outb %0, %1" : : "a"(V), "Nd"(Port));
}
static void Out16(UINT16 Port, UINT16 V) {
    __asm__ volatile("outw %0, %1" : : "a"(V), "Nd"(Port));
}
static void Out32(UINT16 Port, UINT32 V) {
    __asm__ volatile("outl %0, %1" : : "a"(V), "Nd"(Port));
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

static int FindVirtioBlk(UINT16 *IoBar) {
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
                if (Vid != VIRTIO_VID || Did != VIRTIO_BLK_DID) {
                    continue;
                }
                Bar = PciRead32((UINT8)b, d, f, 0x10);
                if ((Bar & 1u) == 0) {
                    return -1; /* 要 IO BAR（legacy） */
                }
                Cmd = (UINT16)(PciRead32((UINT8)b, d, f, 0x04) & 0xFFFF);
                Cmd |= 0x5u; /* IO + BusMaster */
                PciWrite32((UINT8)b, d, f, 0x04,
                           (PciRead32((UINT8)b, d, f, 0x04) & 0xFFFF0000u) | Cmd);
                *IoBar = (UINT16)(Bar & ~3u);
                return 0;
            }
        }
    }
    return -1;
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
    UINT32 Used = 6u + 8u * (UINT32)Num;
    return UsedOff + Used;
}

int HalBlockInit(void) {
    UINT16 Io;
    UINT16 Qnum;
    UINT32 Bytes;
    UINT32 PagesN;
    void *Pages;
    void *Dma;
    UINT64 Phys;
    UINT32 DescBytes;
    UINT32 AvailOff;
    UINT32 UsedOff;

    gReady = 0;
    gIo = 0;
    gQsz = 0;
    if (FindVirtioBlk(&Io) != 0) {
        return -1;
    }
    gIo = Io;

    Out8(gIo + VIRTIO_PCI_STATUS, 0);
    Out8(gIo + VIRTIO_PCI_STATUS, (UINT8)(VIRTIO_ACK | VIRTIO_DRIVER));
    Out32(gIo + VIRTIO_PCI_GUEST_F, 0);

    Out16(gIo + VIRTIO_PCI_Q_SEL, 0);
    Qnum = In16(gIo + VIRTIO_PCI_Q_NUM);
    if (Qnum < 4u || Qnum > QMAX) {
        return -1;
    }
    gQsz = Qnum;

    Bytes = VringBytes(gQsz);
    PagesN = (Bytes + 4095u) / 4096u;
    Pages = HalDmaAllocatePages(PagesN);
    if (Pages == 0) {
        return -1;
    }
    Zero(Pages, (UINTN)PagesN * 4096u);
    Phys = (UINT64)(UINTN)Pages;

    DescBytes = 16u * (UINT32)gQsz;
    AvailOff = DescBytes;
    UsedOff = AlignUp(DescBytes + 6u + 2u * (UINT32)gQsz, VRING_ALIGN);

    gDesc = (VqDesc *)Pages;
    gAvailIdx = (UINT16 *)((UINT8 *)Pages + AvailOff + 2u);
    gAvailRing = (UINT16 *)((UINT8 *)Pages + AvailOff + 4u);
    gUsedIdx = (UINT16 *)((UINT8 *)Pages + UsedOff + 2u);
    gLastUsed = 0;

        Dma = HalDmaAllocatePages(1);
    if (Dma == 0) {
        return -1;
    }
    Zero(Dma, 4096u);
    gDmaReq = (BlkReq *)Dma;
    gDmaStatus = (UINT8 *)Dma + 64;
    gDmaSec = (UINT8 *)Dma + 128;

    Out16(gIo + VIRTIO_PCI_Q_SEL, 0);
    Out32(gIo + VIRTIO_PCI_Q_PFN, (UINT32)(Phys >> 12));
    Out8(gIo + VIRTIO_PCI_STATUS, (UINT8)(VIRTIO_ACK | VIRTIO_DRIVER | VIRTIO_DRIVER_OK));
    gReady = 1;
    return 0;
}

int HalBlockReady(void) {
    return gReady;
}

/* DevWritesData：1=读扇区（设备写缓冲），0=写扇区 */
static int BlkXfer(UINT32 Type, UINT64 Lba, void *Buf, UINT32 Count, int DevWritesData) {
    UINT16 Aidx;
    UINT32 Spin;
    UINT16 Uidx;
    UINT32 Bytes;
    UINT8 *P;
    UINT32 i;

    if (!gReady || Buf == 0 || Count == 0 || Count > 8 || gDmaSec == 0) {
        return -1;
    }

    Bytes = Count * SECTOR_SIZE;
    P = (UINT8 *)Buf;
    if (!DevWritesData) {
        for (i = 0; i < Bytes; i++) {
            gDmaSec[i] = P[i];
        }
    }

    gDmaReq->Type = Type;
    gDmaReq->Reserved = 0;
    gDmaReq->Sector = Lba;
    *gDmaStatus = 0xFF;

    gDesc[0].Addr = (UINT64)(UINTN)gDmaReq;
    gDesc[0].Len = (UINT32)sizeof(BlkReq);
    gDesc[0].Flags = VRING_DESC_F_NEXT;
    gDesc[0].Next = 1;

    gDesc[1].Addr = (UINT64)(UINTN)gDmaSec;
    gDesc[1].Len = Bytes;
    gDesc[1].Flags = DevWritesData
                          ? (UINT16)(VRING_DESC_F_NEXT | VRING_DESC_F_WRITE)
                          : VRING_DESC_F_NEXT;
    gDesc[1].Next = 2;

    gDesc[2].Addr = (UINT64)(UINTN)gDmaStatus;
    gDesc[2].Len = 1;
    gDesc[2].Flags = VRING_DESC_F_WRITE;
    gDesc[2].Next = 0;

    Aidx = *gAvailIdx;
    gAvailRing[Aidx % gQsz] = 0;
    __asm__ volatile("mfence" ::: "memory");
    *gAvailIdx = (UINT16)(Aidx + 1);
    __asm__ volatile("mfence" ::: "memory");
    Out16(gIo + VIRTIO_PCI_Q_NOTIFY, 0);

    for (Spin = 0; Spin < 4000000u; Spin++) {
        __asm__ volatile("mfence" ::: "memory");
        Uidx = *gUsedIdx;
        if (Uidx != gLastUsed) {
            gLastUsed = Uidx;
            break;
        }
        __asm__ volatile("pause");
    }
    if (Spin >= 4000000u || *gDmaStatus != 0) {
        return -1;
    }

    if (DevWritesData) {
        for (i = 0; i < Bytes; i++) {
            P[i] = gDmaSec[i];
        }
    }
    return 0;
}

int HalBlockRead(UINT64 Lba, void *Buf, UINT32 Count) {
    return BlkXfer(VIRTIO_BLK_T_IN, Lba, Buf, Count, 1);
}

int HalBlockWrite(UINT64 Lba, const void *Buf, UINT32 Count) {
    return BlkXfer(VIRTIO_BLK_T_OUT, Lba, (void *)Buf, Count, 0);
}
