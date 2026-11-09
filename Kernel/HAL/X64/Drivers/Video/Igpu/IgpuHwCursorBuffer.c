/*
 * IgpuHwCursorBuffer.c — 光标位图页 / 内建箭头 / 字形拷贝
 *
 * 自 IgpuHwCursor.c 纯搬。64×64×4 须逐 GTT 页映射（见 EnsureBuffer 注释）。
 */
#include "IgpuHwCursorPrivate.h"
#include "Igpu.h"
#include "HalVideo.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"

static void FillBuiltinArrow(UINT32 *Destination) {
    UINT32 y;
    UINT32 x;
    static const char *Rows[16] = {
        "X               ",
        "XX              ",
        "X.X             ",
        "X..X            ",
        "X...X           ",
        "X....X          ",
        "X.....X         ",
        "X......X        ",
        "X.......X       ",
        "X........X      ",
        "X.....XXXX      ",
        "X..X..X         ",
        "X.X X.X         ",
        "XX  X.X         ",
        "X    X.X        ",
        "      XX        ",
    };

    for (y = 0; y < IGPU_CUR_SIZE * IGPU_CUR_SIZE; y++) {
        Destination[y] = 0;
    }
    for (y = 0; y < 16u; y++) {
        for (x = 0; x < 16u; x++) {
            char C = Rows[y][x];
            UINT32 Px;

            if (C == ' ') {
                continue;
            }
            if (C == 'X') {
                Px = 0xFF000000u;
            } else {
                Px = 0xFFFFFFFFu;
            }
            Destination[y * IGPU_CUR_SIZE + x] = Px;
        }
    }
}

void CopyGlyph(const UINT32 *Src, UINT32 Width, UINT32 Height, UINT32 *Destination) {
    UINT32 y;
    UINT32 x;
    UINT32 CopyWidth;
    UINT32 CopyHeight;

    for (y = 0; y < IGPU_CUR_SIZE * IGPU_CUR_SIZE; y++) {
        Destination[y] = 0;
    }
    if (!Src || Width == 0 || Height == 0) {
        FillBuiltinArrow(Destination);
        return;
    }
    CopyWidth = Width;
    CopyHeight = Height;
    if (CopyWidth > IGPU_CUR_SIZE) {
        CopyWidth = IGPU_CUR_SIZE;
    }
    if (CopyHeight > IGPU_CUR_SIZE) {
        CopyHeight = IGPU_CUR_SIZE;
    }
    for (y = 0; y < CopyHeight; y++) {
        for (x = 0; x < CopyWidth; x++) {
            Destination[y * IGPU_CUR_SIZE + x] = Src[y * Width + x];
        }
    }
}

int EnsureBuffer(void) {
    UINT64 Flags;
    UINT32 i;

    if (gArgb) {
        return 1;
    }
    gArgb = (UINT32 *)PhysicalMemoryAllocatePages(IGPU_CUR_PAGES);
    if (!gArgb) {
        return 0;
    }
    gPhys = (UINT64)(UINTN)gArgb;
    Flags = HalVideoFbMapFlags();
    if (VirtualMemoryMapRange(gPhys, gPhys, IGPU_CUR_BYTES, Flags) != 0) {
        gArgb = 0;
        gPhys = 0;
        return 0;
    }
    /*
     * 光标位图是 64×64×4=16KB，横跨 4 个 4KB GTT 页；IgpuGsmMap() 一次只装
     * 一个 PTE。旧代码只映第一页 → 硬件只看到前 16 行：100% 的 12 行箭头
     * 恰好完整，150% 缺尾巴，200% 只剩左上角。必须逐页映射再统一 flush。
     */
    for (i = 0; i < IGPU_CUR_PAGES; i++) {
        UINT64 Off = IGPU_CUR_GTT_OFF + (UINT64)i * 4096ull;

        if (!IgpuGsmMapQuiet(Off, gPhys + (UINT64)i * 4096ull)) {
            gArgb = 0;
            gPhys = 0;
            return 0;
        }
    }
    IgpuGsmFlush();
    return 1;
}
