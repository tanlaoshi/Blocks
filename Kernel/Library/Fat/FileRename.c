/*
 * FileRename.c — 同卷改名（同目录优先就地改 LFN/SFN，避免 DirGrow）
 *
 * FatRename 旧实现：先 DirCreateEntry 再摘旧名 → 双名并存占槽，目录满时
 * DirGrow 整簇清零写 MSC（热传 install renbak/rennew 曾数秒）。
 * 同目录且新名槽数 ≤ 旧名时：原地改写，不扩目录。
 */
#include "Fat.h"
#include "FatPrivate.h"

static int DirectoryContextSame(FAT_DIR_CTX Left, FAT_DIR_CTX Right) {
    return Left.IsFat16Root == Right.IsFat16Root && Left.Cluster == Right.Cluster;
}

static int CountLongNamePrefix(FAT_DIR_CTX Parent, int ShortNameIndex,
                               UINT8 Checksum) {
    int Count = 0;
    int Index;

    for (Index = ShortNameIndex - 1; Index >= 0; Index--) {
        UINT8 Entry[32];

        if (!DirReadEntry(Parent, (UINT32)Index, Entry)) {
            return -1;
        }
        if (!EntryIsLfn(Entry) || Entry[13] != Checksum) {
            break;
        }
        Count++;
        if ((Entry[0] & 0x40) != 0) {
            break;
        }
    }
    return Count;
}

/*
 * 就地改名：NewNeed ≤ OldNeed。成功 FAT_OK；槽不够返回 FAT_ERR_NOSPC 供回退。
 */
static int RenameInPlaceSameDirectory(FAT_DIR_CTX Parent, int ShortNameIndex,
                                      int OldLongNameCount, const char *NewLeaf,
                                      UINT8 Attribute, UINT32 Cluster,
                                      UINT32 Size) {
    UINT8 Name83[11];
    UINT8 Entry[32];
    UINT8 Checksum;
    int NewLongNameCount;
    int OldNeed;
    int NewNeed;
    int FirstSlot;
    int Ordinal;
    int Slot;

    NewLongNameCount = LfnEntryCountForName(NewLeaf);
    if (NewLongNameCount <= 0 || NewLongNameCount > 20) {
        return FAT_ERR_NAMETOOLONG;
    }
    OldNeed = OldLongNameCount + 1;
    NewNeed = NewLongNameCount + 1;
    if (NewNeed > OldNeed) {
        return FAT_ERR_NOSPC;
    }
    if (!PathTo83(NewLeaf, Name83)) {
        if (!Make83Alias(Parent, NewLeaf, Name83)) {
            return FAT_ERR_NOSPC;
        }
    }
    Checksum = Fat83Checksum(Name83);
    FirstSlot = ShortNameIndex - OldLongNameCount;
    for (Ordinal = NewLongNameCount; Ordinal >= 1; Ordinal--) {
        FillLfnEntry(Entry, Ordinal, Ordinal == NewLongNameCount, Checksum,
                     NewLeaf);
        if (!DirWriteEntry(Parent,
                           (UINT32)(FirstSlot + NewLongNameCount - Ordinal),
                           Entry)) {
            return FAT_ERR_IO;
        }
    }
    FillSfnEntry(Entry, Name83, Attribute, Cluster, Size);
    if (!DirWriteEntry(Parent, (UINT32)(FirstSlot + NewLongNameCount), Entry)) {
        return FAT_ERR_IO;
    }
    for (Slot = FirstSlot + NewNeed; Slot < FirstSlot + OldNeed; Slot++) {
        if (!DirReadEntry(Parent, (UINT32)Slot, Entry)) {
            return FAT_ERR_IO;
        }
        Entry[0] = 0xE5;
        if (!DirWriteEntry(Parent, (UINT32)Slot, Entry)) {
            return FAT_ERR_IO;
        }
    }
    return FAT_OK;
}

static int RenameCreateThenUnlink(FAT_DIR_CTX OldParent, const char *OldLeaf,
                                  FAT_DIR_CTX NewParent, const char *NewLeaf,
                                  UINT8 Attribute, UINT32 Cluster,
                                  UINT32 Size) {
    int Error;

    Error = DirCreateEntry(NewParent, NewLeaf, Attribute, Cluster, Size);
    if (Error != FAT_OK) {
        return Error;
    }
    Error = DirUnlinkKeepClusters(OldParent, OldLeaf);
    if (Error != FAT_OK) {
        (void)DirUnlinkKeepClusters(NewParent, NewLeaf);
        return Error;
    }
    return FAT_OK;
}

int FatRename(const char *OldPath, const char *NewPath) {
    FAT_DIR_CTX OldParent;
    FAT_DIR_CTX NewParent;
    char OldLeaf[FAT_NAME_MAX + 1];
    char NewLeaf[FAT_NAME_MAX + 1];
    UINT32 Cluster = 0;
    UINT32 Size = 0;
    UINT8 Attribute = 0;
    UINT8 ShortEntry[32];
    int ShortNameIndex = 0;
    int Existing = 0;
    int OldLongNameCount;
    int Error;

    if (!OldPath || !OldPath[0] || !NewPath || !NewPath[0]) {
        return FAT_ERR_INVAL;
    }
    if (!ResolvePathParentLeaf(OldPath, &OldParent, OldLeaf)) {
        return FAT_ERR_NOENT;
    }
    if (CompIsDotOrDotDot(OldLeaf)) {
        return FAT_ERR_INVAL;
    }
    if (!LookupInDir(OldParent, OldLeaf, &Cluster, &Size, &Attribute)) {
        return FAT_ERR_NOENT;
    }
    if (Attribute & FAT_ATTR_RO) {
        return FAT_ERR_ROFS;
    }
    if (!ResolvePathParentLeaf(NewPath, &NewParent, NewLeaf)) {
        return FAT_ERR_NOENT;
    }
    if (CompIsDotOrDotDot(NewLeaf)) {
        return FAT_ERR_INVAL;
    }
    if (FindDirIndex(NewParent, NewLeaf, 1, &ShortNameIndex, &Existing, 0, 0) &&
        Existing) {
        return FAT_ERR_EXIST;
    }
    if (!DirectoryContextSame(OldParent, NewParent)) {
        return RenameCreateThenUnlink(OldParent, OldLeaf, NewParent, NewLeaf,
                                      Attribute, Cluster, Size);
    }
    if (!FindDirIndex(OldParent, OldLeaf, 1, &ShortNameIndex, &Existing, 0, 0) ||
        !Existing) {
        return FAT_ERR_NOENT;
    }
    if (!DirReadEntry(OldParent, (UINT32)ShortNameIndex, ShortEntry)) {
        return FAT_ERR_IO;
    }
    OldLongNameCount =
        CountLongNamePrefix(OldParent, ShortNameIndex, Fat83Checksum(ShortEntry));
    if (OldLongNameCount < 0) {
        return FAT_ERR_IO;
    }
    Error = RenameInPlaceSameDirectory(OldParent, ShortNameIndex,
                                       OldLongNameCount, NewLeaf, Attribute,
                                       Cluster, Size);
    if (Error == FAT_OK) {
        return FAT_OK;
    }
    if (Error != FAT_ERR_NOSPC) {
        return Error;
    }
    return RenameCreateThenUnlink(OldParent, OldLeaf, NewParent, NewLeaf,
                                  Attribute, Cluster, Size);
}
