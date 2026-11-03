/*
 * Report.c — 串口上那几行：READY / 块 CRC 表 / OK / FAIL / ABORT / RESTORED
 *
 * 文案就是判据（规范 §一.8）：KSEND OK / KSEND FAIL / KSEND RESTORED 三个前缀
 * 是 PC 侧 Tools/ksend.py 的解析键，改字必须两边一起改。
 * 块 CRC 表是二进制（小端），所以走 KsendSerialPutByte 而不是文本行。
 */
#include "Ksend/Private.h"

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

void KsendReadyLine(const KSEND_IMAGE *Image) {
    char Line[KSEND_TEXT_BYTES];
    KSEND_TEXT Out;

    KsendTextReset(&Out, Line, (int)sizeof(Line));
    KsendTextPut(&Out, "KSEND READY");
    KsendTextPutField(&Out, "size", Image->Size);
    KsendTextPutField(&Out, "block", KSEND_BLOCK_BYTES);
    KsendTextPutField(&Out, "n", Image->BlockCount);
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
             * （表黑洞真因曾是 iwl Len>400 拒 MSS 段，见 IwlTxFrame。）
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

void KsendSuccessLine(UINT32 Crc, UINT32 Written, UINT32 Size) {
    char Line[KSEND_TEXT_BYTES];
    KSEND_TEXT Out;

    KsendTextReset(&Out, Line, (int)sizeof(Line));
    KsendTextPut(&Out, "KSEND OK");
    KsendTextPutHexField(&Out, "crc", Crc);
    KsendTextPutField(&Out, "written", Written);
    KsendTextPutField(&Out, "size", Size);
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
