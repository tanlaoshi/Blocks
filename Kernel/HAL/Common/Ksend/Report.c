/*
 * Report.c — 串口上那几行：READY / 块 CRC 表 / OK / FAIL / ABORT / RESTORED
 *
 * 文案就是判据（规范 §一.8）：KSEND OK / KSEND FAIL / KSEND RESTORED 三个前缀
 * 是 PC 侧 Tools/Scripts/ksend.py 的解析键，改字必须两边一起改。
 * 块 CRC 表是二进制（小端），所以走 KsendSerialPutByte 而不是文本行。
 */
#include "Ksend/Private.h"
#include "Hal.h"
#include "HalDevices.h"
#include "Tasks.h"

static const char *FailureName(int Failure) {
    switch (Failure) {
    case KSEND_FAIL_FILE_STATUS:   return "stat";
    case KSEND_FAIL_BACKUP: return "backup";
    case KSEND_FAIL_HEADER: return "header";
    case KSEND_FAIL_DATA: return "data";
    case KSEND_FAIL_WRITE:  return "write";
    case KSEND_FAIL_VERIFY: return "verify";
    case KSEND_FAIL_FLUSH:  return "flush";
    case KSEND_FAIL_SHRINK: return "shrink";
    case KSEND_FAIL_EMPTY:  return "empty";
    case KSEND_FAIL_ABORT:  return "abort";
    case KSEND_FAIL_SEND:   return "send";
    default:                return "unknown";
    }
}

void KsendPrintImage(const char *Tag, const KSEND_IMAGE *Image) {
    char Line[KSEND_TEXT_BYTES];
    KSEND_TEXT Out;

    KsendTextReset(&Out, Line, (int)sizeof(Line));
    KsendTextPut(&Out, Tag);
    KsendTextPutField(&Out, "size", Image->Size);
    KsendTextPutField(&Out, "block", KSEND_BLOCK_BYTES);
    KsendTextPutField(&Out, "n", Image->BlockCount);
    KsendTextPutHexField(&Out, "crc", Image->Crc);
    KsendTextFlush(&Out);
}

void KsendPrintReason(const char *Tag, const char *Reason, int Error) {
    char Line[KSEND_TEXT_BYTES];
    KSEND_TEXT Out;

    KsendTextReset(&Out, Line, (int)sizeof(Line));
    KsendTextPut(&Out, Tag);
    KsendTextPut(&Out, " reason=");
    KsendTextPut(&Out, Reason);
    KsendTextPutSignedField(&Out, "err", Error);
    KsendTextFlush(&Out);
}

UINT32 KsendReadyWindowBlocks(void) {
    if (KsendTransportSocket() < 0) {
        return KSEND_WINDOW_SERIAL;
    }
    if (HalNetWifiShouldOwnTransmit()) {
        return KSEND_WINDOW_WIFI;
    }
    /*
     * 无 Input 专核（Pi VirtDesktop / Duo 单核）：有线大窗攒批落盘打挂
     *（Pi UMING block8；Duo 2026-11-08 block24 data / 1 块 header 超时）。
     * 有 InputTask 的 NUC alx 用 KSEND_WINDOW_WIRED_TCP（现 16）。
     * 门闩 TasksCallerDrainsInput。
     */
    if (TasksCallerDrainsInput()) {
        return KSEND_WINDOW_SERIAL;
    }
    return KSEND_WINDOW_WIRED_TCP;
}

void KsendReadyLine(const KSEND_IMAGE *Image) {
    char Line[KSEND_TEXT_BYTES];
    KSEND_TEXT Out;
    UINT32 Window = KsendReadyWindowBlocks();
    const char *Via;

    if (Window == 0u || Window > KSEND_WINDOW_MAX) {
        Window = 1u;
    }
    /* nuc-2：主机按 via= 分 pace；Wi‑Fi 永不报有线窗 */
    if (KsendTransportSocket() < 0) {
        Via = "serial";
    } else if (HalNetWifiShouldOwnTransmit()) {
        Via = "wifi";
    } else {
        Via = "wired";
    }
    KsendTextReset(&Out, Line, (int)sizeof(Line));
    KsendTextPut(&Out, "KSEND READY");
    KsendTextPutField(&Out, "size", Image->Size);
    KsendTextPutField(&Out, "block", KSEND_BLOCK_BYTES);
    KsendTextPutField(&Out, "n", Image->BlockCount);
    KsendTextPutField(&Out, "win", Window);
    KsendTextPut(&Out, " via=");
    KsendTextPut(&Out, Via);
    KsendTextFlush(&Out);
}

/* n 个小端 u32，然后 1 字节 '\n'；中间不能插任何文本 */
void KsendBlockTable(const KSEND_IMAGE *Image) {
    UINT32 Index;
    UINT32 Byte;
    UINT32 PackedBytes = 0;

    for (Index = 0; Index < Image->BlockCount; Index++) {
        UINT32 Value = Image->BlockCrc[Index];

        for (Byte = 0; Byte < KSEND_WORD_BYTES; Byte++) {
            KsendSerialPutByte(
                (UINT8)((Value >> (Byte * KSEND_BYTE_BITS)) & KSEND_BYTE_MASK));
            PackedBytes++;
            /*
             * 表中每 512 B 只 Drain（进 lwIP + tcp_output），勿 WaitPeer：
             * 中途等 ACK 曾 stalled（NUC Wi‑Fi）。整表末再 WaitPeer。
             * （表黑洞真因曾是 iwl Len>400 拒 MSS 段，见 IwlTransmitFrame。）
             */
            if (PackedBytes >= 512u) {
                KsendSerialDrainTransmitter();
                PackedBytes = 0;
            }
        }
    }
    KsendSerialPutByte(KSEND_TABLE_END);
    KsendSerialDrainTransmitter();
    /* 串口无对端窗；TCP 必须等表字节对端 ACK 后再收块头 */
    KsendSerialWaitPeerAcknowledge();
}

void KsendPhaseNote(const char *Name, UINT32 Milliseconds) {
    char Line[KSEND_TEXT_BYTES];
    KSEND_TEXT Out;

    KsendTextReset(&Out, Line, (int)sizeof(Line));
    KsendTextPut(&Out, "phase ");
    KsendTextPut(&Out, Name);
    KsendTextPutField(&Out, "ms", Milliseconds);
    KsendNote(Line);
}

void KsendPhaseNoteReceive(UINT32 Milliseconds, UINT32 Written) {
    char Line[KSEND_TEXT_BYTES];
    KSEND_TEXT Out;

    KsendTextReset(&Out, Line, (int)sizeof(Line));
    KsendTextPut(&Out, "phase receive");
    KsendTextPutField(&Out, "ms", Milliseconds);
    KsendTextPutField(&Out, "written", Written);
    if (Milliseconds > 0u) {
        /* 每块 4 KiB → kib_per_second = written×4×1000/ms */
        KsendTextPutField(&Out, "kib_per_second",
                          (Written * 4u * 1000u) / Milliseconds);
    }
    KsendNote(Line);
}

void KsendSuccessLine(const KSEND_RUN *Run) {
    char Line[KSEND_TEXT_BYTES];
    KSEND_TEXT Out;

    if (Run == 0) {
        return;
    }
    KsendTextReset(&Out, Line, (int)sizeof(Line));
    KsendTextPut(&Out, "KSEND OK");
    KsendTextPutHexField(&Out, "crc", Run->TrailerCrc);
    KsendTextPutField(&Out, "written", Run->Written);
    KsendTextPutField(&Out, "size", Run->NewSize);
    KsendTextPutField(&Out, "prepare", Run->PrepareMilliseconds);
    KsendTextPutField(&Out, "ready", Run->ReadyTableMilliseconds);
    KsendTextPutField(&Out, "receive", Run->ReceiveMilliseconds);
    KsendTextPutField(&Out, "finish", Run->FinishMilliseconds);
    KsendTextPutField(&Out, "total", Run->TotalMilliseconds);
    KsendTextFlush(&Out);
}

void KsendFailLine(int Failure, UINT32 Block, UINT32 Want, UINT32 Got, int Error) {
    char Line[KSEND_TEXT_BYTES];
    KSEND_TEXT Out;

    KsendTextReset(&Out, Line, (int)sizeof(Line));
    KsendTextPut(&Out, "KSEND FAIL");
    if (Failure == KSEND_FAIL_VERIFY) {
        /* 规范文案：at/want/got 只在 CRC 对不上时才有意义 */
        KsendTextPutField(&Out, "at", Block);
        KsendTextPutHexField(&Out, "want", Want);
        KsendTextPutHexField(&Out, "got", Got);
    } else {
        KsendTextPut(&Out, " reason=");
        KsendTextPut(&Out, FailureName(Failure));
        if (Failure == KSEND_FAIL_WRITE) {
            KsendTextPutField(&Out, "at", Block);
        }
        KsendTextPutSignedField(&Out, "err", Error);
    }
    KsendTextFlush(&Out);
}

void KsendAbortLine(UINT32 Written) {
    char Line[KSEND_TEXT_BYTES];
    KSEND_TEXT Out;

    KsendTextReset(&Out, Line, (int)sizeof(Line));
    KsendTextPut(&Out, "KSEND ABORT");
    KsendTextPutField(&Out, "written", Written);
    KsendTextFlush(&Out);
}

void KsendRestoredLine(const KSEND_IMAGE *Image) {
    char Line[KSEND_TEXT_BYTES];
    KSEND_TEXT Out;

    KsendTextReset(&Out, Line, (int)sizeof(Line));
    KsendTextPut(&Out, "KSEND RESTORED");
    KsendTextPutField(&Out, "size", Image->Size);
    KsendTextPutHexField(&Out, "crc", Image->Crc);
    KsendTextFlush(&Out);
}
