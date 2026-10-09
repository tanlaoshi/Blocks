/*
 * Utf8.c — K26：UTF-8 → Unicode 码点（1～4 字节）
 */
#include "Utf8.h"

UINTN Utf8Decode(const char *S, UINT32 *OutCp) {
    UINT8 C0;
    UINT32 Cp;
    UINTN Need;
    UINTN i;

    if (S == 0 || OutCp == 0) {
        return 0;
    }
    C0 = (UINT8)S[0];
    if (C0 == 0) {
        return 0;
    }
    if (C0 < 0x80u) {
        *OutCp = C0;
        return 1;
    }
    if ((C0 & 0xE0u) == 0xC0u) {
        Cp = (UINT32)(C0 & 0x1Fu);
        Need = 2;
    } else if ((C0 & 0xF0u) == 0xE0u) {
        Cp = (UINT32)(C0 & 0x0Fu);
        Need = 3;
    } else if ((C0 & 0xF8u) == 0xF0u) {
        Cp = (UINT32)(C0 & 0x07u);
        Need = 4;
    } else {
        return 0;
    }
    for (i = 1; i < Need; i++) {
        UINT8 Cx = (UINT8)S[i];
        if ((Cx & 0xC0u) != 0x80u) {
            return 0;
        }
        Cp = (Cp << 6) | (UINT32)(Cx & 0x3Fu);
    }
    *OutCp = Cp;
    return Need;
}
