/*
 * FileWriteAtCluster.c — PR-KSEND-FAT-CURSOR：扩链 + 顺序写游标
 *
 * Seek 命中不够：EnsureClusterCount 曾每次从首簇扫 Need → 连续 WriteAt 仍 O(n²)。
 * 本文件：簇链已知长度/末簇游标；向前扩只从末簇续。
 */
#include "Fat.h"
#include "FatPrivate.h"

/* —— 顺序 Seek（同文件 Offset==Next 命中）—— */
static UINT32 sSequentialFirstCluster;
static UINT32 sSequentialNextOffset;
static UINT32 sSequentialCluster;
static UINT32 sSequentialOffInCluster;
static int sSequentialValid;

/* —— 簇链前缀游标（EnsureClusterCount）—— */
static UINT32 sChainFirstCluster;
static UINT32 sChainKnownCount;
static UINT32 sChainLastCluster;
static int sChainValid;

void FatSequentialWriteInvalidate(void) {
    sSequentialValid = 0;
    sChainValid = 0;
}

static void SequentialWriteRemember(UINT32 FirstCluster, UINT32 NextOffset,
                                    UINT32 Cluster, UINT32 OffInCluster) {
    sSequentialFirstCluster = FirstCluster;
    sSequentialNextOffset = NextOffset;
    sSequentialCluster = Cluster;
    sSequentialOffInCluster = OffInCluster;
    sSequentialValid = 1;
}

int FatSeekWriteOffset(UINT32 FirstCluster, UINTN Offset, UINT32 *OutCluster,
                       UINT32 *OutOffInCluster) {
    if (sSequentialValid && sSequentialFirstCluster == FirstCluster &&
        (UINT32)Offset == sSequentialNextOffset) {
        *OutCluster = sSequentialCluster;
        *OutOffInCluster = sSequentialOffInCluster;
        return 1;
    }
    return SeekFileOffset(FirstCluster, Offset, OutCluster, OutOffInCluster);
}

void FatSequentialWriteRemember(UINT32 FirstCluster, UINT32 NextOffset,
                                UINT32 Cluster, UINT32 OffInCluster) {
    SequentialWriteRemember(FirstCluster, NextOffset, Cluster, OffInCluster);
}

static void ChainCursorRemember(UINT32 FirstCluster, UINT32 KnownCount,
                                UINT32 LastCluster) {
    sChainFirstCluster = FirstCluster;
    sChainKnownCount = KnownCount;
    sChainLastCluster = LastCluster;
    sChainValid = (FirstCluster >= 2u && KnownCount > 0u && LastCluster >= 2u) ? 1 : 0;
}

static int ZeroAndStoreNewCluster(UINT32 Cluster) {
    UINT32 ZeroIndex;
    UINT32 Bytes = ClusterBytes();

    for (ZeroIndex = 0; ZeroIndex < Bytes; ZeroIndex++) {
        gCluster[ZeroIndex] = 0;
    }
    if (!StoreCluster(Cluster)) {
        return 0;
    }
    FatIoBreath();
    return 1;
}

static int AllocateFreshChain(UINT32 *InOutFirst, UINT32 NeedClusters) {
    UINT32 First = 0;
    UINT32 Previous = 0;
    UINT32 Index;
    UINT32 Cluster;

    for (Index = 0; Index < NeedClusters; Index++) {
        Cluster = FatAllocCluster();
        if (Cluster < 2u) {
            if (First >= 2u) {
                FatFreeChain(First);
            }
            return 0;
        }
        if (Index == 0u) {
            First = Cluster;
        } else if (!FatSet(Previous, Cluster)) {
            FatFreeChain(First);
            return 0;
        }
        Previous = Cluster;
        if (!ZeroAndStoreNewCluster(Cluster)) {
            FatFreeChain(First);
            return 0;
        }
    }
    if (!FatSet(Previous, EocValue())) {
        FatFreeChain(First);
        return 0;
    }
    *InOutFirst = First;
    ChainCursorRemember(First, NeedClusters, Previous);
    return 1;
}

/*
 * 保证从 First 起至少 NeedClusters 个簇。成功更新 *InOutFirst。
 * 命中链游标时只续扩 Need-Known，禁止每次从头 FatNext。
 */
int FatEnsureClusterCount(UINT32 *InOutFirst, UINT32 NeedClusters) {
    UINT32 First;
    UINT32 Previous;
    UINT32 Cluster;
    UINT32 Have;

    if (NeedClusters == 0u) {
        return 1;
    }
    First = *InOutFirst;
    if (First < 2u) {
        return AllocateFreshChain(InOutFirst, NeedClusters);
    }

    if (sChainValid && sChainFirstCluster == First && sChainKnownCount >= NeedClusters) {
        return 1;
    }

    if (sChainValid && sChainFirstCluster == First && sChainKnownCount > 0u) {
        Have = sChainKnownCount;
        Previous = sChainLastCluster;
        Cluster = Previous;
    } else {
        Have = 0u;
        Previous = 0u;
        Cluster = First;
    }

    while (Have < NeedClusters) {
        if (Have == 0u) {
            Cluster = First;
        } else {
            UINT32 Next = FatNext(Previous);

            if (Next == FAT_CLUSTER_INVALID) {
                FatSequentialWriteInvalidate();
                return 0;
            }
            if (ClusterEnd(Next) || Next < 2u) {
                UINT32 NewCluster = FatAllocCluster();

                if (NewCluster < 2u) {
                    return 0;
                }
                if (!FatSet(Previous, NewCluster)) {
                    FatFreeChain(NewCluster);
                    return 0;
                }
                if (!ZeroAndStoreNewCluster(NewCluster)) {
                    FatFreeChain(NewCluster);
                    return 0;
                }
                Cluster = NewCluster;
                if (!FatSet(Cluster, EocValue())) {
                    return 0;
                }
            } else {
                Cluster = Next;
            }
        }
        Previous = Cluster;
        Have++;
    }

    *InOutFirst = First;
    ChainCursorRemember(First, Have, Previous);
    return 1;
}
