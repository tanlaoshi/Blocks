/*
 * MassStorageAutoPolicy.c — 开机 MSC auto 策略与 claim 判定（fs-msc-defer 序 2–4）
 *
 * 自 FileSystem.c 纯搬。行为契约（启动盘 BLOCKS / 勿误 skip USB 棒）勿改。
 */
#include "FileSystemPrivate.h"
#include "SerialLog.h"
#include "HalDevices.h"

/* PR-H-msc-7b：默认卷上 MSC.OFF 或 THEME.CFG 含 msc=0 → 倾向关 auto */
static int MassStoragePolicySaysOff(void) {
    UINT8 Buffer[512];
    UINTN Size = 0;
    UINTN Index;

    if (FileSystemReadFile("MSC.OFF", Buffer, 1, &Size) == FAT_OK) {
        return 1;
    }
    Size = 0;
    if (FileSystemReadFile("THEME.CFG", Buffer, sizeof(Buffer) - 1, &Size) !=
        FAT_OK) {
        return 0;
    }
    Buffer[Size] = 0;
    for (Index = 0; Index + 5 <= Size; Index++) {
        if (Buffer[Index] == 'm' && Buffer[Index + 1] == 's' &&
            Buffer[Index + 2] == 'c' && Buffer[Index + 3] == '=' &&
            Buffer[Index + 4] == '0') {
            return 1;
        }
    }
    return 0;
}

int AnyVolumeHasBlocksId(void) {
    int i;

    for (i = 0; i < gVolCount; i++) {
        if (gVols[i].HasBlocksId) {
            return 1;
        }
    }
    return 0;
}

/*
 * 第一轮挂卷成功后：读盘上 MSC.OFF / msc=0（序 2）。
 * 无第一轮卷时不读盘（与旧分支一致）。
 */
int MassStorageAutoApplyPolicy(int Auto, int HasBlocks) {
    if (!Auto || !MassStoragePolicySaysOff()) {
        return Auto;
    }
    if (HasBlocks) {
        HalUsbMscAutoSet(0);
        LogBoot("Boot: MSC Auto Off (MSC=0/MSC.OFF)\n");
        return 0;
    }
    LogBoot("Boot: MSC Auto Keep (No BLOCKS.ID; Ignore MSC=0)\n");
    return Auto;
}

/*
 * 序 3–4：仅当「启动盘上」已有 Os/BLOCKS 才 skip MSC。
 * 坑：U 盘起时 NVMe 常先挂上 BLOCKS/ROOT.CFG → 旧逻辑当 Os 已挂而跳过 claim，
 * 启动棒永不进 Mux，Files 只剩 NUC 分区；旁路依赖桌面 Hot 也易空。
 * 启动盘尚无卷（BootDiskHasBlocksId==0）→ 必须 claim。旁路第二只盘仍桌面 Secondary。
 */
int MassStorageAutoResolveClaim(int Auto, int HasBlocks) {
    int BootHas;

    if (!HasBlocks && !Auto) {
        HalUsbMscAutoSet(1);
        LogBoot("Boot: MSC Auto Force (Need BLOCKS)\n");
        return 1;
    }
    if (!Auto) {
        return 0;
    }
    BootHas = FsBootDiskHasBlocksId();
    /* 序码供 BOOT.LOG 对表：1=策略 2=开机claim 3=桌面secondary 4=热插拔 */
    if (BootHas == 1) {
        LogBoot("FS: msc/1 skip (boot disk has BLOCKS — expect NUC/SSD path)\n");
        return 0;
    }
    if (BootHas == 0) {
        LogBoot("FS: msc/1 claim (boot disk not mounted — expect USB-stick path)\n");
        return 1;
    }
    /* 旧 Boot 无 DiskGUID：回退「任意 Os 已挂则 skip」 */
    if (FsOsVolumeMounted()) {
        LogBoot("FS: msc/1 skip (OS volume mounted, no BootDiskGuid)\n");
        return 0;
    }
    LogBoot("FS: msc/1 claim (need OS volume)\n");
    return 1;
}
