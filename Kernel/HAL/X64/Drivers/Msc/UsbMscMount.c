/*
 * UsbMscMount.c — MSC 挂载 / AutoBeforeFs / Release
 *
 * 从 UsbMsc.c 纯搬；顺序契约不变：Notify 在 Release 前；RemoveMsc 后再清后端。
 */
#include "Msc/UsbMsc.h"
#include "UsbMscPrivate.h"
#include "Block.h"
#include "BlockMux.h"
#include "SerialLog.h"

void BlockMscInstall(void); /* BlockMsc.c */

int UsbMscMount(void) {
    UINT32 Bsz;

    if (!UsbMscReady()) {
        return -1;
    }
    if (UsbMscBlockCount() == 0 || UsbMscBlockSize() == 0) {
        if (UsbMscCapacity() < 0) {
            return -2;
        }
    }
    Bsz = UsbMscBlockSize();
    if (Bsz != BLOCK_SECTOR_SIZE) {
        return -3;
    }
    BlockMscInstall();
    return 0;
}

int UsbMscAutoBeforeFs(void) {
    int Claim;
    int Rc;

    if (!UsbMscAutoEnabled()) {
        return 1;
    }

    Claim = UsbMscClaim();
    if (Claim < 0) {
        /* 无 HC 或 claim 中 cmd sick（已 Recover，勿当「空」再扫） */
        LogBoot("Boot: MSC Auto claim abort\n");
        return -1;
    }
    if (Claim == 0) {
        return 1;
    }

    Rc = UsbMscMount();
    if (Rc != 0) {
        LogBoot("Boot: MSC Auto Mux Fail\n");
        (void)UsbMscRelease();
        return Rc;
    }
    LogBoot("Boot: MSC Auto Mux OK\n");
    return 0;
}

int UsbMscRelease(void) {
    const MSC_HOST *H = CurrentHost();

    BlockMuxRemoveMsc();
    if (H && H->Release) {
        (void)H->Release();
    }
    gMscBe = 0;
    return 0;
}
