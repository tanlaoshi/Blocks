/*
 * FileWriteFast.c — PR-KSEND-FAT-WRITE-FAST：延后目录 Size、预扩链、可否跳过清簇
 */
#include "Fat.h"
#include "FatPrivate.h"
#include "Block.h"

static int sClusterAllocateZero = 1;
static int sDirectorySizeDefer;
static int sDirectorySizeDirty;
static FAT_DIR_CTX sDirectorySizeParent;
static UINT32 sDirectorySizeIndex;
static UINT32 sDirectorySizeFirstCluster;
static UINT32 sDirectorySizePending;

void FatSetClusterAllocateZero(int On) {
    sClusterAllocateZero = On ? 1 : 0;
}

int FatClusterAllocateZero(void) {
    return sClusterAllocateZero;
}

void FatWriteDirectorySizeDefer(int On) {
    if (!On && sDirectorySizeDirty) {
        (void)FatFlushDeferredDirectorySize();
    }
    sDirectorySizeDefer = On ? 1 : 0;
}

void FatDirectorySizeDeferRemember(FAT_DIR_CTX Parent, UINT32 Index,
                                   UINT32 FirstCluster, UINT32 Size) {
    if (!sDirectorySizeDefer) {
        return;
    }
    sDirectorySizeParent = Parent;
    sDirectorySizeIndex = Index;
    sDirectorySizeFirstCluster = FirstCluster;
    sDirectorySizePending = Size;
    sDirectorySizeDirty = 1;
}

int FatDirectorySizeDeferActive(void) {
    return sDirectorySizeDefer;
}

int FatFlushDeferredDirectorySize(void) {
    UINT8 Entry[32];

    if (!sDirectorySizeDirty) {
        return FAT_OK;
    }
    if (!DirReadEntry(sDirectorySizeParent, sDirectorySizeIndex, Entry)) {
        sDirectorySizeDirty = 0;
        return FAT_ERR_IO;
    }
    Entry[11] = FAT_ATTR_ARCH;
    if (gFatType == 32) {
        Write16(Entry + 20,
                (UINT16)((sDirectorySizeFirstCluster >> 16) & FAT_U16_MASK));
    }
    Write16(Entry + 26, (UINT16)(sDirectorySizeFirstCluster & FAT_U16_MASK));
    Write32(Entry + 28, sDirectorySizePending);
    if (!DirWriteEntry(sDirectorySizeParent, sDirectorySizeIndex, Entry)) {
        return FAT_ERR_IO;
    }
    sDirectorySizeDirty = 0;
    return FAT_OK;
}

int FatEnsureFileByteSize(const char *Path, UINT32 Size) {
    FAT_DIR_CTX Parent;
    char Leaf[FAT_NAME_MAX + 1];
    UINT32 FirstCluster = 0;
    UINT32 OldSize = 0;
    UINT8 OldAttr = 0;
    int Existing = 0;
    int Index = 0;
    UINT32 NeedClusters;
    UINT32 ClusterBytesCount;
    UINT8 Entry[32];
    int Result;

    if (!Path || Size == 0u || Size > FAT_WRITE_MAX) {
        return FAT_ERR_INVAL;
    }
    if (!ResolvePathParentLeaf(Path, &Parent, Leaf)) {
        return FAT_ERR_NOENT;
    }
    if (CompIsDotOrDotDot(Leaf)) {
        return FAT_ERR_INVAL;
    }
    if (FindDirIndex(Parent, Leaf, 1, &Index, &Existing, &FirstCluster, &OldAttr) &&
        Existing) {
        if (OldAttr & FAT_ATTR_DIR) {
            return FAT_ERR_ISDIR;
        }
        if (OldAttr & FAT_ATTR_RO) {
            return FAT_ERR_ROFS;
        }
        if (DirReadEntry(Parent, (UINT32)Index, Entry)) {
            OldSize = Read32(Entry + 28);
            FirstCluster = EntryCluster(Entry);
        }
    } else {
        Existing = 0;
        FirstCluster = 0;
        OldSize = 0;
    }

    ClusterBytesCount = ClusterBytes();
    NeedClusters = (Size + ClusterBytesCount - 1u) / ClusterBytesCount;
    if (NeedClusters == 0u) {
        NeedClusters = 1u;
    }
    if (!FatEnsureClusterCount(&FirstCluster, NeedClusters)) {
        return FAT_ERR_NOSPC;
    }

    if (Existing) {
        if (!DirReadEntry(Parent, (UINT32)Index, Entry)) {
            return FAT_ERR_IO;
        }
        Entry[11] = FAT_ATTR_ARCH;
        if (gFatType == 32) {
            Write16(Entry + 20, (UINT16)((FirstCluster >> 16) & FAT_U16_MASK));
        }
        Write16(Entry + 26, (UINT16)(FirstCluster & FAT_U16_MASK));
        if (Size > OldSize) {
            Write32(Entry + 28, Size);
        }
        if (!DirWriteEntry(Parent, (UINT32)Index, Entry)) {
            return FAT_ERR_IO;
        }
        return FAT_OK;
    }

    Result = DirCreateEntry(Parent, Leaf, FAT_ATTR_ARCH, FirstCluster, Size);
    if (Result != FAT_OK) {
        FatFreeChain(FirstCluster);
        return Result;
    }
    return FAT_OK;
}
