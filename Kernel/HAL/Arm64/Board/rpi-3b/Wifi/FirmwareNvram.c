/*
 * FirmwareNvram.c — 盘上 .txt → 芯片 NVRAM 字节（去注释、NUL 行尾）
 */
#include "Wifi/Private.h"

int WifiNvramPrepare(const UINT8 *Text, UINTN TextBytes, UINT8 *Out, UINT32 OutCapacity,
                     UINT32 *OutBytes) {
    UINTN Index;
    UINT32 Written = 0;
    int InComment = 0;
    int LineHasContent = 0;

    if (Text == 0 || Out == 0 || OutBytes == 0 || OutCapacity < 4u) {
        return -1;
    }

    for (Index = 0; Index < TextBytes; Index++) {
        UINT8 Byte = Text[Index];

        if (Byte == '#' && !LineHasContent) {
            InComment = 1;
            continue;
        }
        if (Byte == '\r') {
            continue;
        }
        if (Byte == '\n') {
            if (LineHasContent && !InComment) {
                if (Written >= OutCapacity) {
                    return -1;
                }
                Out[Written++] = 0;
            }
            InComment = 0;
            LineHasContent = 0;
            continue;
        }
        if (InComment) {
            continue;
        }
        if (Byte == ' ' || Byte == '\t') {
            if (!LineHasContent) {
                continue;
            }
        }
        if (Written >= OutCapacity) {
            return -1;
        }
        Out[Written++] = Byte;
        LineHasContent = 1;
    }
    if (LineHasContent && !InComment) {
        if (Written >= OutCapacity) {
            return -1;
        }
        Out[Written++] = 0;
    }
    /* 对齐到 4 字节；尾填 0 */
    while ((Written & 3u) != 0) {
        if (Written >= OutCapacity) {
            return -1;
        }
        Out[Written++] = 0;
    }
    *OutBytes = Written;
    return 0;
}
