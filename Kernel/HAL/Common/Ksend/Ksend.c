/*
 * Ksend.c — `ksend info` / `ksend selftest` 的实现 + 通用人话行
 *
 * 主文件只做编排：读写全在 KsendFile.c，协议行全在 KsendReport.c，
 * 协议主循环在 KsendTransfer.c。完整传输的入口也在这里（KsendTransfer）。
 *
 * 为什么先在真机上跑 selftest：Duo 上 FAT 写路径从未验证过；自检失败时
 * 真传一定也会失败，而且是在拿仅有的 Kernel.elf 冒险。
 */
#include "Ksend/Private.h"
#include "FileSystem.h"

void KsendNote(const char *Text) {
    char Line[KSEND_TEXT_BYTES];
    KSEND_TEXT Out;

    KsendTextReset(&Out, Line, (int)sizeof(Line));
    KsendTextPut(&Out, "ksend: ");
    KsendTextPut(&Out, Text);
    KsendTextFlush(&Out);
}

/* 只读：Kernel.elf 的 size / n / 整体 CRC。PC 用它和自己的 ELF 对一下再决定传不传 */
int KsendInfo(void) {
    UINT32 Size = 0;
    int Err;

    KsendSerialDrainInput(KSEND_DRAIN_QUIET_MS, KSEND_DRAIN_LIMIT_MS);
    Err = KsendFileSize(KSEND_KERNEL_PATH, &Size);
    if (Err == FAT_ERR_NOENT) {
        Size = 0;
        Err = FAT_OK;
    }
    if (Err == FAT_OK) {
        /* info 不并发，借新图当只打印的草稿，省一份 8 KB 静态数组 */
        Err = KsendFileScan(KSEND_KERNEL_PATH, Size, &gKsendNewImage);
    }
    if (Err != FAT_OK) {
        KsendPrintReason("KSEND FAIL", "stat", Err);
        return 1;
    }
    KsendPrintImage("ksend: Kernel.elf", &gKsendNewImage);
    if (Size == 0u) {
        KsendNote("Kernel.elf missing: a full transfer will create it");
    }
    KsendPrintImage("KSEND INFO", &gKsendNewImage);
    return 0;
}

/* `ksend selftest`：判 FS 写路径（新建 + 覆写 + 读回 + 删） */
int KsendSelftest(void) {
    KSEND_SELFTEST Result;
    char Line[KSEND_TEXT_BYTES];
    KSEND_TEXT Out;
    int Err;

    KsendSerialDrainInput(KSEND_DRAIN_QUIET_MS, KSEND_DRAIN_LIMIT_MS);
    KsendNote("selftest: BLOCKS:KSELFT.BIN 8192 B create + overwrite + readback");
    Err = KsendFileSelfTest(&Result);
    if (Err != FAT_OK) {
        KsendTextReset(&Out, Line, (int)sizeof(Line));
        KsendTextPut(&Out, "KSEND SELFTEST FAIL");
        KsendTextPutField(&Out, "step", Result.Step);
        if (Result.FailOffset != KSEND_FAIL_OFFSET_NONE) {
            KsendTextPutField(&Out, "at", Result.FailOffset);
            KsendTextPutHexField(&Out, "want", Result.FailWant);
            KsendTextPutHexField(&Out, "got", Result.FailGot);
        } else {
            KsendTextPutSignedField(&Out, "err", Err);
        }
        KsendTextFlush(&Out);
        return 1;
    }
    KsendTextReset(&Out, Line, (int)sizeof(Line));
    KsendTextPut(&Out, "KSEND SELFTEST OK");
    KsendTextPutField(&Out, "size", Result.Size);
    KsendTextPutHexField(&Out, "crc", Result.Crc);
    KsendTextFlush(&Out);
    return 0;
}
