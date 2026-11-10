/*
 * FileWriteAt.c — 偏移写（PR-S-fatfile-1 / PR-KSEND-FAT-CURSOR）
 *
 * 顺序 Seek + 扩链游标见 FileWriteAtCluster.c。
 */
#include "Fat.h"
#include "FatPrivate.h"
#include "Block.h"

int FatWriteFileAt(const char *Path, UINTN Offset, const void *Buffer, UINTN Len,
                   UINTN *OutN) {
    FAT_DIR_CTX Parent;
    char Leaf[FAT_NAME_MAX + 1];
    UINT32 FirstCluster = 0;
    UINT32 OldSize = 0;
    UINT8 OldAttr = 0;
    int Existing = 0;
    int Index = 0;
    UINT32 FinalSize;
    UINT32 NeedClusters;
    UINT32 ClusterBytesCount;
    UINT32 Cluster;
    UINT32 OffInCluster;
    const UINT8 *Source = (const UINT8 *)Buffer;
    UINTN Done = 0;
    UINT8 Entry[32];

    if (OutN) {
        *OutN = 0;
    }
    if (!Path || (!Buffer && Len > 0)) {
        return FAT_ERR_INVAL;
    }
    if (Len == 0) {
        return FAT_OK;
    }
    if (Offset > FAT_WRITE_MAX || Len > FAT_WRITE_MAX ||
        Offset + Len > FAT_WRITE_MAX) {
        return FAT_ERR_FILE_TOO_BIG;
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
        } else {
            OldSize = 0;
        }
    } else {
        Existing = 0;
        FirstCluster = 0;
        OldSize = 0;
        (void)BlockFlush();
        if (FindDirIndex(Parent, Leaf, 1, &Index, &Existing, &FirstCluster,
                         &OldAttr) &&
            Existing) {
            if (OldAttr & FAT_ATTR_DIR) {
                return FAT_ERR_ISDIR;
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
    }

    FinalSize = (UINT32)(Offset + Len);
    if (FinalSize < OldSize) {
        FinalSize = OldSize;
    }
    if (FinalSize > FAT_WRITE_MAX) {
        return FAT_ERR_FILE_TOO_BIG;
    }

    ClusterBytesCount = ClusterBytes();
    NeedClusters = (FinalSize + ClusterBytesCount - 1) / ClusterBytesCount;
    if (NeedClusters == 0) {
        NeedClusters = 1;
    }
    if (!FatEnsureClusterCount(&FirstCluster, NeedClusters)) {
        return FAT_ERR_NOSPC;
    }

    if (!FatSeekWriteOffset(FirstCluster, Offset, &Cluster, &OffInCluster)) {
        FatSequentialWriteInvalidate();
        return FAT_ERR_IO;
    }
    while (Done < Len) {
        UINT32 Chunk;
        UINT32 ByteIndex;
        UINT32 StartOff = OffInCluster;

        if (!LoadCluster(Cluster)) {
            FatSequentialWriteInvalidate();
            return FAT_ERR_IO;
        }
        Chunk = ClusterBytesCount - OffInCluster;
        if (Chunk > Len - Done) {
            Chunk = (UINT32)(Len - Done);
        }
        for (ByteIndex = 0; ByteIndex < Chunk; ByteIndex++) {
            gCluster[OffInCluster + ByteIndex] = Source[Done + ByteIndex];
        }
        if (!StoreCluster(Cluster)) {
            FatSequentialWriteInvalidate();
            return FAT_ERR_IO;
        }
        FatIoBreath();
        Done += Chunk;
        OffInCluster = StartOff + Chunk;
        if (Done >= Len) {
            if (OffInCluster >= ClusterBytesCount) {
                UINT32 NextCluster = FatNext(Cluster);

                if (NextCluster == FAT_CLUSTER_INVALID || ClusterEnd(NextCluster) ||
                    NextCluster < 2) {
                    FatSequentialWriteInvalidate();
                } else {
                    FatSequentialWriteRemember(FirstCluster, (UINT32)(Offset + Done),
                                               NextCluster, 0);
                }
            } else {
                FatSequentialWriteRemember(FirstCluster, (UINT32)(Offset + Done),
                                           Cluster, OffInCluster);
            }
            break;
        }
        Cluster = FatNext(Cluster);
        OffInCluster = 0;
        if (Cluster == FAT_CLUSTER_INVALID || ClusterEnd(Cluster) || Cluster < 2) {
            FatSequentialWriteInvalidate();
            return FAT_ERR_IO;
        }
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
        Write32(Entry + 28, FinalSize);
        if (!DirWriteEntry(Parent, (UINT32)Index, Entry)) {
            return FAT_ERR_IO;
        }
    } else {
        int Result =
            DirCreateEntry(Parent, Leaf, FAT_ATTR_ARCH, FirstCluster, FinalSize);
        if (Result != FAT_OK) {
            FatFreeChain(FirstCluster);
            return Result;
        }
    }

    if (OutN) {
        *OutN = Done;
    }
    return FAT_OK;
}
