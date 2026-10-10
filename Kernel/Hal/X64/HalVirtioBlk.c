/*
 * HalVirtioBlk.c — X64 legacy virtio-blk（K16；K44 最多 2 盘）
 *
 * 【初学者】PCI 扫全部 1af4:1001；HalBlockSelect 切换当前盘。
 */
#include "HalBlock.h"
#include "HalDma.h"

#define VIRTIO_VID          0x1AF4u
#define VIRTIO_BLK_DID      0x1001u
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

typedef struct {
    int Ready;
    UINT16 Io;
    UINT16 Qsz;
    VqDesc *Desc;
    UINT16 *AvailIdx;
    UINT16 *AvailRing;
    UINT16 *UsedIdx;
    UINT16 LastUsed;
    BlkReq *DmaReq;
    UINT8 *DmaStatus;
    UINT8 *DmaSec;
} VBLK;

static VBLK gDev[HAL_BLOCK_MAX_DRIVES];
static int gN;
static int gCur;

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

static int CollectIoBars(UINT16 *Out, int Max) {
    UINT16 b;
    UINT8 d, f;
    int N = 0;

    for (b = 0; b < 8 && N < Max; b++) {
        for (d = 0; d < 32 && N < Max; d++) {
            for (f = 0; f < 8 && N < Max; f++) {
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
                    continue;
                }
                Cmd = (UINT16)(PciRead32((UINT8)b, d, f, 0x04) & 0xFFFF);
                Cmd |= 0x5u;
                PciWrite32((UINT8)b, d, f, 0x04,
                           (PciRead32((UINT8)b, d, f, 0x04) & 0xFFFF0000u) | Cmd);
                Out[N++] = (UINT16)(Bar & ~3u);
            }
        }
    }
    return N;
}

static int InitOne(VBLK *D, UINT16 Io) {
    UINT16 Qnum;
    UINT32 Bytes;
    UINT32 PagesN;
    void *Pages;
    void *Dma;
    UINT64 Phys;
    UINT32 DescBytes;
    UINT32 AvailOff;
    UINT32 UsedOff;

    Zero(D, sizeof(*D));
    D->Io = Io;
    Out8(Io + VIRTIO_PCI_STATUS, 0);
    Out8(Io + VIRTIO_PCI_STATUS, (UINT8)(VIRTIO_ACK | VIRTIO_DRIVER));
    Out32(Io + VIRTIO_PCI_GUEST_F, 0);
    Out16(Io + VIRTIO_PCI_Q_SEL, 0);
    Qnum = In16(Io + VIRTIO_PCI_Q_NUM);
    if (Qnum < 4u || Qnum > QMAX) {
        return -1;
    }
    D->Qsz = Qnum;
    Bytes = VringBytes(D->Qsz);
    PagesN = (Bytes + 4095u) / 4096u;
    Pages = HalDmaAllocatePages(PagesN);
    if (Pages == 0) {
        return -1;
    }
    Zero(Pages, (UINTN)PagesN * 4096u);
    Phys = (UINT64)(UINTN)Pages;
    DescBytes = 16u * (UINT32)D->Qsz;
    AvailOff = DescBytes;
    UsedOff = AlignUp(DescBytes + 6u + 2u * (UINT32)D->Qsz, VRING_ALIGN);
    D->Desc = (VqDesc *)Pages;
    D->AvailIdx = (UINT16 *)((UINT8 *)Pages + AvailOff + 2u);
    D->AvailRing = (UINT16 *)((UINT8 *)Pages + AvailOff + 4u);
    D->UsedIdx = (UINT16 *)((UINT8 *)Pages + UsedOff + 2u);
    D->LastUsed = 0;
    Dma = HalDmaAllocatePages(1);
    if (Dma == 0) {
        return -1;
    }
    Zero(Dma, 4096u);
    D->DmaReq = (BlkReq *)Dma;
    D->DmaStatus = (UINT8 *)Dma + 64;
    D->DmaSec = (UINT8 *)Dma + 128;
    Out16(Io + VIRTIO_PCI_Q_SEL, 0);
    Out32(Io + VIRTIO_PCI_Q_PFN, (UINT32)(Phys >> 12));
    Out8(Io + VIRTIO_PCI_STATUS,
         (UINT8)(VIRTIO_ACK | VIRTIO_DRIVER | VIRTIO_DRIVER_OK));
    D->Ready = 1;
    return 0;
}

int HalBlockInit(void) {
    UINT16 Bars[HAL_BLOCK_MAX_DRIVES];
    int Found;
    int i;

    gN = 0;
    gCur = 0;
    Zero(gDev, sizeof(gDev));
    Found = CollectIoBars(Bars, HAL_BLOCK_MAX_DRIVES);
    for (i = 0; i < Found; i++) {
        if (InitOne(&gDev[gN], Bars[i]) == 0) {
            gN++;
        }
    }
    return gN > 0 ? 0 : -1;
}

int HalBlockReady(void) {
    return gN > 0 && gCur >= 0 && gCur < gN && gDev[gCur].Ready;
}

int HalBlockDriveCount(void) {
    return gN;
}

int HalBlockSelect(int Drive) {
    if (Drive < 0 || Drive >= gN || !gDev[Drive].Ready) {
        return -1;
    }
    gCur = Drive;
    return 0;
}

int HalBlockCurrent(void) {
    return gCur;
}

static int BlkXfer(UINT32 Type, UINT64 Lba, void *Buf, UINT32 Count, int DevWrites) {
    VBLK *D = &gDev[gCur];
    UINT16 Aidx;
    UINT32 Spin;
    UINT16 Uidx;
    UINT32 Bytes;
    UINT8 *P;
    UINT32 i;

    if (!HalBlockReady() || Buf == 0 || Count == 0 || Count > 8) {
        return -1;
    }
    Bytes = Count * SECTOR_SIZE;
    P = (UINT8 *)Buf;
    if (!DevWrites) {
        for (i = 0; i < Bytes; i++) {
            D->DmaSec[i] = P[i];
        }
    }
    D->DmaReq->Type = Type;
    D->DmaReq->Reserved = 0;
    D->DmaReq->Sector = Lba;
    *D->DmaStatus = 0xFF;
    D->Desc[0].Addr = (UINT64)(UINTN)D->DmaReq;
    D->Desc[0].Len = (UINT32)sizeof(BlkReq);
    D->Desc[0].Flags = VRING_DESC_F_NEXT;
    D->Desc[0].Next = 1;
    D->Desc[1].Addr = (UINT64)(UINTN)D->DmaSec;
    D->Desc[1].Len = Bytes;
    D->Desc[1].Flags = DevWrites ? (UINT16)(VRING_DESC_F_NEXT | VRING_DESC_F_WRITE)
                                  : VRING_DESC_F_NEXT;
    D->Desc[1].Next = 2;
    D->Desc[2].Addr = (UINT64)(UINTN)D->DmaStatus;
    D->Desc[2].Len = 1;
    D->Desc[2].Flags = VRING_DESC_F_WRITE;
    D->Desc[2].Next = 0;
    Aidx = *D->AvailIdx;
    D->AvailRing[Aidx % D->Qsz] = 0;
    __asm__ volatile("mfence" ::: "memory");
    *D->AvailIdx = (UINT16)(Aidx + 1);
    __asm__ volatile("mfence" ::: "memory");
    Out16(D->Io + VIRTIO_PCI_Q_NOTIFY, 0);
    for (Spin = 0; Spin < 4000000u; Spin++) {
        __asm__ volatile("mfence" ::: "memory");
        Uidx = *D->UsedIdx;
        if (Uidx != D->LastUsed) {
            D->LastUsed = Uidx;
            break;
        }
        __asm__ volatile("pause");
    }
    if (Spin >= 4000000u || *D->DmaStatus != 0) {
        return -1;
    }
    if (DevWrites) {
        for (i = 0; i < Bytes; i++) {
            P[i] = D->DmaSec[i];
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
