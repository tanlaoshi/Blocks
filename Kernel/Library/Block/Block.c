/*
 * Block.c — 块设备抽象（后端由 HAL 注册）
 *
 * 多后端并存：AHCI + NVMe 等累积进表；>1 时 Fan 映槽位到 (Backend, LocalDrive)。
 * Mux（Primary+MSC）经 BlockRegisterOuterBackend 包在最外，不进 Fan 表。
 *
 * 5h ❌：曾对 Read/Write/Flush 套 HalIrqSave；NUC remove 仍卡 + ~1s 顿挫
 * （疑 cli 下 MSC WaitBulk ~500ms 超时）。已撤。
 */
#include "Block.h"
#include "LibWrite.h"

#define BLOCK_MAX_BACKENDS 4

typedef struct {
    const BLOCK_BACKEND *Backend;
    UINT32 LocalDrive;
} BLOCK_DRIVE_SLOT;

static const BLOCK_BACKEND *gRegisteredBackends[BLOCK_MAX_BACKENDS];
static UINT32 gRegisteredBackendCount;
static const BLOCK_BACKEND *gActiveBackend;
static const BLOCK_BACKEND *gOuterBackend;
static BLOCK_DRIVE_SLOT gDriveSlots[BLOCK_MAX_DRIVES];
static UINT8 gDriveSlotReady[BLOCK_MAX_DRIVES];
static UINT8 gReady[BLOCK_MAX_DRIVES];
static UINT32 gDrive;

static int BlockFanProbe(UINT32 Drive);
static int BlockFanReadSectors(UINT32 Drive, UINT32 Lba, UINT32 Count, void *Buffer);
static int BlockFanWriteSectors(UINT32 Drive, UINT32 Lba, UINT32 Count,
                                const void *Buffer);
static int BlockFanFlush(UINT32 Drive);

static const BLOCK_BACKEND gFanBackend = {
    .Probe = BlockFanProbe,
    .ReadSectors = BlockFanReadSectors,
    .WriteSectors = BlockFanWriteSectors,
    .Flush = BlockFanFlush,
};

static void BlockFanRebuildSlots(void) {
    UINT32 Slot;
    UINT32 BackendIndex;
    UINT32 LocalDrive;

    for (Slot = 0; Slot < BLOCK_MAX_DRIVES; Slot++) {
        gDriveSlots[Slot].Backend = 0;
        gDriveSlots[Slot].LocalDrive = 0;
        gDriveSlotReady[Slot] = 0;
    }
    Slot = 0;
    for (BackendIndex = 0;
         BackendIndex < gRegisteredBackendCount && Slot < BLOCK_MAX_DRIVES;
         BackendIndex++) {
        const BLOCK_BACKEND *Backend = gRegisteredBackends[BackendIndex];

        if (!Backend || !Backend->Probe) {
            continue;
        }
        for (LocalDrive = 0;
             LocalDrive < BLOCK_MAX_DRIVES && Slot < BLOCK_MAX_DRIVES;
             LocalDrive++) {
            if (!Backend->Probe(LocalDrive)) {
                continue;
            }
            gDriveSlots[Slot].Backend = Backend;
            gDriveSlots[Slot].LocalDrive = LocalDrive;
            gDriveSlotReady[Slot] = 1;
            Slot++;
        }
    }
}

static int BlockFanProbe(UINT32 Drive) {
    if (Drive == 0) {
        BlockFanRebuildSlots();
    }
    if (Drive >= BLOCK_MAX_DRIVES) {
        return 0;
    }
    return gDriveSlotReady[Drive] ? 1 : 0;
}

static int BlockFanReadSectors(UINT32 Drive, UINT32 Lba, UINT32 Count,
                               void *Buffer) {
    const BLOCK_DRIVE_SLOT *Slot;

    if (Drive >= BLOCK_MAX_DRIVES || !gDriveSlotReady[Drive]) {
        return 0;
    }
    Slot = &gDriveSlots[Drive];
    return Slot->Backend->ReadSectors(Slot->LocalDrive, Lba, Count, Buffer);
}

static int BlockFanWriteSectors(UINT32 Drive, UINT32 Lba, UINT32 Count,
                                const void *Buffer) {
    const BLOCK_DRIVE_SLOT *Slot;

    if (Drive >= BLOCK_MAX_DRIVES || !gDriveSlotReady[Drive]) {
        return 0;
    }
    Slot = &gDriveSlots[Drive];
    return Slot->Backend->WriteSectors(Slot->LocalDrive, Lba, Count, Buffer);
}

static int BlockFanFlush(UINT32 Drive) {
    const BLOCK_DRIVE_SLOT *Slot;

    if (Drive >= BLOCK_MAX_DRIVES || !gDriveSlotReady[Drive]) {
        return 1;
    }
    Slot = &gDriveSlots[Drive];
    if (!Slot->Backend->Flush) {
        return 1;
    }
    return Slot->Backend->Flush(Slot->LocalDrive);
}

static const BLOCK_BACKEND *BlockComposePrimaryBackend(void) {
    if (gRegisteredBackendCount > 1) {
        return &gFanBackend;
    }
    if (gRegisteredBackendCount == 1) {
        return gRegisteredBackends[0];
    }
    return 0;
}

static void BlockRefreshActiveBackend(void) {
    if (gOuterBackend) {
        gActiveBackend = gOuterBackend;
        return;
    }
    gActiveBackend = BlockComposePrimaryBackend();
}

void BlockRegisterBackend(const BLOCK_BACKEND *Backend) {
    UINT32 i;

    if (!Backend) {
        return;
    }
    for (i = 0; i < gRegisteredBackendCount; i++) {
        if (gRegisteredBackends[i] == Backend) {
            BlockRefreshActiveBackend();
            return;
        }
    }
    if (gRegisteredBackendCount >= BLOCK_MAX_BACKENDS) {
        LibWrite("block: backend table full\n");
        return;
    }
    gRegisteredBackends[gRegisteredBackendCount++] = Backend;
    BlockRefreshActiveBackend();
}

void BlockRegisterOuterBackend(const BLOCK_BACKEND *Backend) {
    gOuterBackend = Backend;
    gActiveBackend = Backend ? Backend : BlockComposePrimaryBackend();
}

UINT32 BlockSelectedDrive(void) {
    return gDrive;
}

const BLOCK_BACKEND *BlockPeekBackend(void) {
    return gActiveBackend;
}

const BLOCK_BACKEND *BlockPeekPrimaryBackend(void) {
    return BlockComposePrimaryBackend();
}

int BlockBackendReady(void) {
    return gActiveBackend != 0;
}

int BlockSelect(UINT32 Drive) {
    if (Drive >= BLOCK_MAX_DRIVES || !gReady[Drive]) {
        return 0;
    }
    gDrive = Drive;
    return 1;
}

UINT32 BlockCurrentDrive(void) {
    return gDrive;
}

int BlockInitialize(void) {
    UINT32 n = 0;
    UINT32 d;

    if (!gActiveBackend) {
        LibWrite("block: no backend registered\n");
        return 0;
    }

    for (d = 0; d < BLOCK_MAX_DRIVES; d++) {
        gReady[d] = 0;
        if (gActiveBackend->Probe(d)) {
            gReady[d] = 1;
            n++;
            LibWrite("block: drive ");
            LibWriteHex32(d);
            LibWrite(" ready\n");
        }
    }
    gDrive = 0;
    if (n == 0) {
        LibWrite("block: no drives\n");
        return 0;
    }
    if (!gReady[0]) {
        for (d = 0; d < BLOCK_MAX_DRIVES; d++) {
            if (gReady[d]) {
                gDrive = d;
                break;
            }
        }
    }
    return (int)n;
}

int BlockDriveReady(UINT32 Drive) {
    if (Drive >= BLOCK_MAX_DRIVES) {
        return 0;
    }
    return gReady[Drive] ? 1 : 0;
}

int BlockReadSectors(UINT32 Lba, UINT32 Count, void *Buffer) {
    if (!gActiveBackend) {
        return 0;
    }
    return gActiveBackend->ReadSectors(gDrive, Lba, Count, Buffer);
}

int BlockWriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer) {
    if (!gActiveBackend) {
        return 0;
    }
    return gActiveBackend->WriteSectors(gDrive, Lba, Count, Buffer);
}

int BlockFlush(void) {
    if (!gActiveBackend) {
        return 0;
    }
    if (!gActiveBackend->Flush) {
        return 1;
    }
    return gActiveBackend->Flush(gDrive);
}
