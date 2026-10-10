/*
 * FatPrivate.h — FileSystem 夹内 FAT 写路径共享声明
 *
 * 【初学者】
 * - 分层：Core/FileSystem 内部；User/Hal 勿 include
 * - 对外业务 API 仍在 FatFile.h（Write/MakeDirectory/Delete/Rename）
 * - 本头只给 FatSlot / FatWrite / FatMakeDirectory / FatDelete / FatRename 共用
 */
#ifndef FAT_PRIVATE_H
#define FAT_PRIVATE_H

#include "FatVolume.h"

/* 根目录一次查找：命中项 + 首个空闲槽（新建用） */
typedef struct {
    const char *Want83;
    int Have;
    int Free;
    UINT32 EntLba;
    UINT32 EntOff;
    UINT32 FreeLba;
    UINT32 FreeOff;
    UINT8 Ent[32];
} FAT_SLOT;

int FatActiveVolumeOpen(FAT_VOLUME *Volume);
int FatActiveVolumeReadOnly(void);
int FatPathResolve83(const char *Path, char Name83[11]);
void FatMemoryZero(UINT8 *P, UINTN N);
int FatDirectorySlotFind(FAT_VOLUME *Volume, const char Name83[11], FAT_SLOT *Slot);
int FatDirectoryEntryPut(UINT32 Lba, UINT32 Off, const UINT8 Ent[32]);
void FatDirectoryEntryFill(UINT8 Ent[32], const char Name83[11], UINT8 Attr,
                           UINT32 Cluster, UINT32 Size);

#endif
