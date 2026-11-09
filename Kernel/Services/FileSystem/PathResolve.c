/*
 * PathResolve.c — 卷前缀解析（`BLOCKS:` / `BOOT:` / 盘符 / 下标）
 *
 * 自 FileSystem.c 纯搬。BLOCKS: 必须跟 OsTarget / 启动盘，禁止 first-match。
 */
#include "FileSystemPrivate.h"
#include "BootHandoff.h"

/*
 * 解析前缀：
 *   BLOCKS:path / BOOT:path / ESP:path / A:path / B:path
 *   0:path / 1:path（卷下标）
 * 无冒号 → 默认卷，整串为相对路径。
 */
int FileSystemResolve(const char *Path, int *OutVol, const char **OutRel) {
    const char *Colon;
    char Pref[FS_VOL_NAME_MAX];
    int PrefLen;
    int i;
    int Vol;

    if (!Path || !OutVol || !OutRel) {
        return FAT_ERR_INVAL;
    }
    if (gVolCount <= 0) {
        return FAT_ERR_IO;
    }

    Colon = Path;
    while (*Colon && *Colon != ':') {
        Colon++;
    }
    if (*Colon != ':') {
        *OutVol = gDefaultVol;
        *OutRel = Path;
        return FAT_OK;
    }

    PrefLen = (int)(Colon - Path);
    if (PrefLen <= 0 || PrefLen >= FS_VOL_NAME_MAX) {
        return FAT_ERR_INVAL;
    }
    for (i = 0; i < PrefLen; i++) {
        Pref[i] = Path[i];
    }
    Pref[PrefLen] = 0;

    Vol = -1;
    if (StrEqIgnoreCase(Pref, "BOOT")) {
        Vol = FsFindBootVolume();
    } else if (PrefLen == 1 && Pref[0] >= '0' && Pref[0] <= '9') {
        Vol = Pref[0] - '0';
    } else if (PrefLen == 1 &&
               ((Pref[0] >= 'A' && Pref[0] <= 'Z') ||
                (Pref[0] >= 'a' && Pref[0] <= 'z'))) {
        char L = Pref[0];
        if (L >= 'a') {
            L = (char)(L - 'a' + 'A');
        }
        for (i = 0; i < gVolCount; i++) {
            if (gVols[i].Letter == L) {
                Vol = i;
                break;
            }
        }
    } else if (StrEqIgnoreCase(Pref, "BLOCKS")) {
        /*
         * 同名卷取 OsTarget，其次启动盘，再次 OsDisk；有 BootDiskGuid 时
         * 禁止 first-match（U 盘起 NVMe 常先入表，BLOCKS: 曾误打到 SSD）。
         */
        int Os = FileSystemOsVol();
        const BOOT_HANDOFF *Info = BootHandoffGet();
        int BootGuidOk = (Info && Info->BootDiskGuidOk) ? 1 : 0;

        if (Os >= 0 && Os < gVolCount &&
            StrEqIgnoreCase(gVols[Os].Name, Pref)) {
            Vol = Os;
        }
        if (Vol < 0) {
            for (i = 0; i < gVolCount; i++) {
                if (StrEqIgnoreCase(Pref, gVols[i].Name) &&
                    FileSystemVolOnBootDisk(i) == 1) {
                    Vol = i;
                    break;
                }
            }
        }
        if (Vol < 0) {
            for (i = 0; i < gVolCount; i++) {
                if (StrEqIgnoreCase(Pref, gVols[i].Name) &&
                    FsVolOnOsDisk(i)) {
                    Vol = i;
                    break;
                }
            }
        }
        if (Vol < 0 && !BootGuidOk) {
            for (i = 0; i < gVolCount; i++) {
                if (StrEqIgnoreCase(Pref, gVols[i].Name)) {
                    Vol = i;
                    break;
                }
            }
        }
    } else {
        for (i = 0; i < gVolCount; i++) {
            if (StrEqIgnoreCase(Pref, gVols[i].Name)) {
                Vol = i;
                break;
            }
        }
    }
    if (Vol < 0 || Vol >= gVolCount) {
        return FAT_ERR_NOENT;
    }

    *OutVol = Vol;
    *OutRel = Colon + 1;
    if (**OutRel == '/' || **OutRel == '\\') {
        (*OutRel)++;
    }
    return FAT_OK;
}
