/*
 * Command.c — Admin/IO 命令与扇区传送（PR-S-nvme-1）
 *
 * Block 层 LBA/Count 恒为 512B。NS LBADS=9 直通；LBADS>9（常见 12=4KiB）
 * 在本文件折算 DeviceLba，非整设备块写走读改写。勿改 GPT/FAT。
 */
#include "Nvme.h"
#include "Private.h"
#include "Block.h"
#include "PCIe.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "Debug.h"
#include "Hal.h"
#include "HalSerial.h"
#include "SerialLog.h"

static void DbWrite(NVME_CTRL *C, UINT32 Qid, int Completion, UINT16 Val) {
    UINT32 Off = 0x1000u + ((2u * Qid + (Completion ? 1u : 0u)) * (4u << C->Dstrd));
    MmioW32(C->Bar, Off, Val);
}

static UINT16 AllocCid(NVME_CTRL *C) {
    UINT16 Id = C->NextCid++;
    if (C->NextCid == 0) {
        C->NextCid = 1;
    }
    return Id;
}

static int WaitCqe(NVME_CTRL *C, NVME_CQE *Cq, UINT16 *Head, UINT8 *Phase, UINT16 WantCid, int Io) {
    int Spin = (int)C->TimeoutLoops;
    while (Spin-- > 0) {
        volatile NVME_CQE *Entry = &Cq[*Head];
        UINT16 Sp = Entry->StatusPhase;
        if ((Sp & 1u) == (*Phase & 1u)) {
            UINT16 Cid = Entry->Cid;
            UINT16 Status = (UINT16)(Sp >> 1);
            UINT16 Next = (UINT16)((*Head + 1u) % NVME_QSIZE);
            if (Next == 0) {
                *Phase ^= 1u;
            }
            *Head = Next;
            DbWrite(C, Io ? 1u : 0u, 1, *Head);
            if (Cid != WantCid) {
                continue;
            }
            return Status == 0 ? 1 : 0;
        }
        HalCpuRelax();
    }
    return 0;
}

int AdminCmd(NVME_CTRL *C, NVME_SQE *Cmd) {
    UINT16 Cid = AllocCid(C);
    UINT16 Tail = C->AsqTail;

    Cmd->Cdw0 = (UINT32)(Cmd->Cdw0 & 0xFFu) | ((UINT32)Cid << 16);
    CopyMemory(&C->Asq[Tail], Cmd, sizeof(*Cmd));
    Fence();
    Tail = (UINT16)((Tail + 1u) % NVME_QSIZE);
    C->AsqTail = Tail;
    DbWrite(C, 0, 0, Tail);
    return WaitCqe(C, C->Acq, &C->AcqHead, &C->AcqPhase, Cid, 0);
}

static int IoCmd(NVME_CTRL *C, NVME_SQE *Cmd) {
    UINT16 Cid = AllocCid(C);
    UINT16 Tail = C->IosqTail;

    Cmd->Cdw0 = (UINT32)(Cmd->Cdw0 & 0xFFu) | ((UINT32)Cid << 16);
    CopyMemory(&C->Iosq[Tail], Cmd, sizeof(*Cmd));
    Fence();
    Tail = (UINT16)((Tail + 1u) % NVME_QSIZE);
    C->IosqTail = Tail;
    DbWrite(C, 1, 0, Tail);
    return WaitCqe(C, C->Iocq, &C->IocqHead, &C->IocqPhase, Cid, 1);
}

/*
 * 以设备 LBA 为单位：Bounce 已填好（写）或读入 Bounce（读）。
 * DeviceCount * 设备扇区 ≤ PAGE_SIZE。
 */
static int NvmeIssueDeviceIo(NVME_CTRL *C, UINT32 DeviceLba, UINT32 DeviceCount,
                             int Write) {
    NVME_SQE Cmd;
    UINT32 DeviceBytes = 1u << C->LbaShift;

    if (DeviceCount == 0 || DeviceCount * DeviceBytes > PAGE_SIZE) {
        return 0;
    }
    ZeroMemory(&Cmd, sizeof(Cmd));
    Cmd.Cdw0 = Write ? NVME_OPC_WRITE : NVME_OPC_READ;
    Cmd.Nsid = C->NsId;
    Cmd.Prp1 = (UINT64)(UINTN)C->Bounce;
    Cmd.Cdw10 = DeviceLba;
    Cmd.Cdw11 = 0;
    Cmd.Cdw12 = DeviceCount - 1u;
    return IoCmd(C, &Cmd);
}

/* LBADS=9：Host LBA 即设备 LBA；Bounce 一次最多 NVME_BOUNCE_SECTORS。 */
static int NvmeTransferNative512(NVME_CTRL *C, UINT32 Lba, UINT32 Count,
                                 void *Buffer, int Write) {
    UINT8 *Data = (UINT8 *)Buffer;
    UINT32 Done = 0;
    UINT32 SectorBytes = BLOCK_SECTOR_SIZE;

    while (Done < Count) {
        UINT32 Chunk = Count - Done;
        UINT32 Bytes;

        if (Chunk > NVME_BOUNCE_SECTORS) {
            Chunk = NVME_BOUNCE_SECTORS;
        }
        Bytes = Chunk * SectorBytes;
        if (Write) {
            CopyMemory(C->Bounce, Data + (UINTN)Done * SectorBytes, Bytes);
        }
        if (!NvmeIssueDeviceIo(C, Lba + Done, Chunk, Write)) {
            return 0;
        }
        if (!Write) {
            CopyMemory(Data + (UINTN)Done * SectorBytes, C->Bounce, Bytes);
        }
        Done += Chunk;
    }
    return 1;
}

/*
 * LBADS>9：按设备块折算。Host 扇区落在设备块内偏移时，写路径先读整块再合并。
 */
static int NvmeTransferTranslate(NVME_CTRL *C, UINT32 HostLba, UINT32 HostCount,
                                 void *Buffer, int Write) {
    UINT8 *Data = (UINT8 *)Buffer;
    UINT32 HostDone = 0;
    UINT32 DeviceBytes = 1u << C->LbaShift;
    UINT32 HostPerDevice = DeviceBytes / BLOCK_SECTOR_SIZE;

    if (HostPerDevice == 0 || (DeviceBytes % BLOCK_SECTOR_SIZE) != 0) {
        return 0;
    }
    if (DeviceBytes > PAGE_SIZE) {
        DebugWrite("nvme: device LBA larger than bounce\n");
        return 0;
    }

    while (HostDone < HostCount) {
        UINT32 CurHostLba = HostLba + HostDone;
        UINT32 DeviceLba = CurHostLba / HostPerDevice;
        UINT32 HostOffsetInDevice = CurHostLba % HostPerDevice;
        UINT32 HostRemainInDevice = HostPerDevice - HostOffsetInDevice;
        UINT32 HostChunk = HostCount - HostDone;
        UINT32 ByteOffset;
        UINT32 ByteCount;
        int Partial;

        if (HostChunk > HostRemainInDevice) {
            HostChunk = HostRemainInDevice;
        }
        ByteOffset = HostOffsetInDevice * BLOCK_SECTOR_SIZE;
        ByteCount = HostChunk * BLOCK_SECTOR_SIZE;
        Partial = (HostOffsetInDevice != 0) || (HostChunk != HostPerDevice);

        if (Write) {
            if (Partial) {
                if (!NvmeIssueDeviceIo(C, DeviceLba, 1, 0)) {
                    return 0;
                }
            }
            CopyMemory(C->Bounce + ByteOffset, Data + (UINTN)HostDone * BLOCK_SECTOR_SIZE,
                       ByteCount);
            if (!NvmeIssueDeviceIo(C, DeviceLba, 1, 1)) {
                return 0;
            }
        } else {
            if (!NvmeIssueDeviceIo(C, DeviceLba, 1, 0)) {
                return 0;
            }
            CopyMemory(Data + (UINTN)HostDone * BLOCK_SECTOR_SIZE, C->Bounce + ByteOffset,
                       ByteCount);
        }
        HostDone += HostChunk;
    }
    return 1;
}

int NvmeTransfer(NVME_CTRL *C, UINT32 Lba, UINT32 Count, void *Buffer, int Write) {
    if (!C || !C->Ready || !Buffer || Count == 0) {
        return 0;
    }
    if (C->LbaShift < 9 || C->LbaShift > 12) {
        DebugWrite("nvme: unsupported LbaShift\n");
        return 0;
    }
    if (C->LbaShift == 9) {
        return NvmeTransferNative512(C, Lba, Count, Buffer, Write);
    }
    return NvmeTransferTranslate(C, Lba, Count, Buffer, Write);
}
