/*
 * Utf8.c — K26：UTF-8 → Unicode 码点（1～4 字节）
 *
 * 【初学者】
 * - Core/Video：Font/Gui 解析 UTF-8 路径与输入。
 * - 入口：Utf8Decode（返回消耗字节数，0 表示非法/结束）。
 * - 边界：不做 NFC/NFKC；非法序列返回 0。
 */
#include "Utf8.h"

/*
 * Utf8Decode — 从 S 取一个 Unicode 码点
 *
 * 做什么：识别 1～4 字节 UTF-8；非法续字节或过长序列返回 0。
 * 谁调用：FontDrawStringAt、WindowPaint 标题行、FontTtfPreheatUtf8、Desktop 测宽。
 * 前后文：
 *   前 — 调用方持有以 NUL 或行尾结束的缓冲区；
 *   后 — 按返回值推进指针再画字或预热缓存。
 *   兄弟 — FontDrawCodepointAt（用 OutCp 画）。
 * 返回：消耗字节数；0 表示 NUL/非法（OutCp 未写）。
 */
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
