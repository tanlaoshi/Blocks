/*
 * FileSystemPrivate.h — FileSystem 内部（仅 Services/FileSystem）
 *
 * 对外 API 仍在 FileSystem.h。User 勿 include。
 */
#ifndef FILE_SYSTEM_PRIVATE_H
#define FILE_SYSTEM_PRIVATE_H

#include "FileSystem.h"
#include "CString.h"
#include "VirtualFileSystem.h"
#include "Gpt.h"

typedef struct {
    UINT32 Drive;
    UINT32 StartLba;
    char   Name[FS_VOL_NAME_MAX]; /* 前缀名，不含冒号：BLOCKS / A / ESP / RES */
    char   Letter;                /* 'A'+index，便于 A: 访问 */
    int    ReadOnly;
    int    HasBlocksId;
    const FS_OPS *Ops;            /* PR-F3：每卷后端（fat / res） */
} FS_VOLUME;

extern FS_VOLUME gVols[FS_MAX_VOLUMES];
extern int gVolCount;
extern int gDefaultVol;
extern int gActiveVol;
extern UINT32 gActiveDrive;
extern UINT32 gActiveLba;
extern const FS_OPS *gActiveOps;

int MountAllVolumes(void);
int FileSystemPathBlocked(const char *Path);

/* PR-F-fs-1：单卷 / 默认卷（MountVolume.c / MountPick.c） */
void FsMountOneFatPart(UINT32 Drive, const GPT_FAT_PART *Part, int *BlocksVol);
void FsMountAddResVolume(void);
void FsMountPickDefault(int BlocksVol, UINT8 *Tmp, UINTN TmpSz);

/* Path.c 已有全局 StrEqIgnoreCase，这里不能再导出。 */
static inline int StrEqIgnoreCase(const char *A, const char *B) {
    while (*A && *B) {
        char Ca = *A;
        char Cb = *B;
        if (Ca >= 'a' && Ca <= 'z') {
            Ca = (char)(Ca - 'a' + 'A');
        }
        if (Cb >= 'a' && Cb <= 'z') {
            Cb = (char)(Cb - 'a' + 'A');
        }
        if (Ca != Cb) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}


#endif
