/*
 * HalVideo.h — 帧缓冲 HAL 门面（Common 经 Hal.h 使用，不直接 include 驱动头）
 */
#ifndef HAL_VIDEO_H
#define HAL_VIDEO_H

#include "BootHandoff.h"

void HalVideoSet(const VIDEO_CONFIG *Config);
/* Pi：VC scanout 常为 RGB 低字节=R；UI 为 0x00RRGGBB 低字节=B → 写 FB 对调 */
void HalVideoSetFbSwapRb(int On);
/* PR-G9：分配并启用后缓冲；Present 提交脏区到 GOP */
void HalVideoInitializeBackbuffer(void);
/* 推进一轮 HAL 脏→GOP（可 Partial）；Shell 环末用。对照 gui-g6-present */
void HalVideoPresent(void);
/* 光标 Restore/Paint 后立刻小框 Present（绕开 Defer 大 AABB）；Hw 启用时 Gui 勿调 */
void HalVideoPresentCursor(void);
/*
 * 硬件光标（hw-cursor-*）：有则热路径只 SetPos；无/失败 → Gui 走软件末波。
 * Supported：探测后；Enable 失败不得自称 Supported。
 * X64+igpu 目标真 CUR_*；Virtio/Arm/RiscV stub → Supported=0。
 */
int HalVideoHwCursorSupported(void);
int HalVideoHwCursorEnable(const UINT32 *Argb, UINT32 Width, UINT32 Height,
                           UINT32 HotX, UINT32 HotY);
void HalVideoHwCursorSetPos(UINT32 X, UINT32 Y);
void HalVideoHwCursorDisable(void);
/* 循环刷完脏区（合成/关窗收官）；勿在 Shell 热路径滥用 */
void HalVideoPresentFlush(void);
/* 丢掉脏区不写 LFB（开机：Boot 已 GOP 铺底，只同步后缓冲） */
void HalVideoDiscardDirty(void);
/* 标记脏矩形（拖窗并旧∪新 footprint） */
void HalVideoMarkDirty(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
/* 拖窗：Rows 极大 → 整脏区一次 blit，减轻左右条带频闪；0=恢复默认 64 */
void HalVideoSetPresentChunkRows(UINT32 Rows);
/* PR-GUI-l2-font：1=点阵边缘灰度；0=硬 1bpp */
void HalVideoSetGlyphSmooth(int On);
void HalVideoDrawBeginFront(void);
void HalVideoDrawEndFront(void);
/* 只收笔、不 FbDcacheSync（开机行日志批量 sync 用） */
void HalVideoDrawEndFrontNoSync(void);
void HalVideoFbDcacheSync(void);
int HalVideoBackbufferEnabled(void);
/* 后缓冲物理/恒等虚址；未启用则 0（igpu Present 用） */
UINT64 HalVideoBackbufferBase(void);
void HalVideoGetSize(UINT32 *Width, UINT32 *Height);
/* UI 整体缩放（50/100/150/200）；成功 0，重配后缓冲 */
UINT32 HalVideoGetUiScale(void);
/* PR-23b：整数绘制倍数（200%→2，其余→1）；TTF 栅格尺寸 = 逻辑字高 × 本值 */
UINT32 HalVideoGetDrawScale(void);
void HalVideoGetPhysicalSize(UINT32 *Width, UINT32 *Height);
int HalVideoSetUiScale(UINT32 Percent);
/* PR-G-hotres：QEMU/Bochs VGA 可热切则 1；真机/无 DISPI 为 0 */
int HalVideoCanHotSetMode(void);
/*
 * PR-G-hotres：运行中改分辨率（Bochs DISPI）。成功 0。
 * 调用方先 VirtualMemoryMapRange 覆盖足够大的 LFB。
 */
int HalVideoSetMode(UINT32 Width, UINT32 Height);
/* PR-G-modes：Boot 枚举的可用模式数；0 表示未提供（Settings 用内置表） */
UINT32 HalVideoModeCount(void);
int HalVideoModeGet(UINT32 Index, UINT32 *Width, UINT32 *Height);
UINT64 HalVideoFrameBufferBase(void);
UINT64 HalVideoFrameBufferSize(void);
/* GPU 翻页后切换 CPU gFront 与 FrameBufferBase */
void HalVideoSetScanout(UINT32 *Va, UINT32 PitchPx, UINT64 Phys, UINT64 Size);
/* PR-G-fb-pte：boot 一行 FB phys + PWT/PCD(/PAT) + 推导 cache；不改映射。x86 有内容，其它 HAL 空实现 */
void HalVideoLogFbPte(void);
/* 填入一行（无尾 '\n'）；成功返回长度，无 FB 返回 0 */
int HalVideoFbPteLine(char *Buf, UINTN Max);
/*
 * PR-G-fb-wc：PAT 开 WC 后仅重映 LFB（PWT→WC）；MMIO/xHCI 不动。
 * 映射标志供 Theme 热切等复用。
 */
void HalVideoEnableFbWc(void);
UINT64 HalVideoFbMapFlags(void);

void HalVideoDrawPixel(UINT32 X, UINT32 Y, UINT32 Color);
/* 忽略客户区 clip（鼠标光标） */
void HalVideoDrawPixelRaw(UINT32 X, UINT32 Y, UINT32 Color);
void HalVideoXorPixelRaw(UINT32 X, UINT32 Y, UINT32 Mask);
/* 光标 save-under / 实心绘制：脏区走光标矩形，避免与 Shell 并成近全屏 */
void HalVideoCursorOverlayBegin(void);
void HalVideoCursorOverlayEnd(void);
UINT32 HalVideoReadPixel(UINT32 X, UINT32 Y);
void HalVideoFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color);
/* PR-GUI-alpha：后缓冲 Src-over-Dst；Alpha=0 空操作，255=不透明 Fill */
UINT32 HalVideoBlendRgb(UINT32 Dst, UINT32 Src, UINT8 Alpha);
void HalVideoBlendPixel(UINT32 X, UINT32 Y, UINT32 Color, UINT8 Alpha);
void HalVideoBlendPixelRaw(UINT32 X, UINT32 Y, UINT32 Color, UINT8 Alpha);
void HalVideoBlendFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                           UINT32 Color, UINT8 Alpha);
void HalVideoCopyRect(UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                      UINT32 Width, UINT32 Height);
void HalVideoReadRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 *Out);
void HalVideoWriteRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT32 *In);
/*
 * PR-23c：物理像素矩形读写（save-under 用）。坐标已是物理像素
 * （调用方按 HalVideoGetDrawScale 换算）；读回再写回逐像素还原，
 * 200% 下不会把块内细节抹平。越界裁剪到绘制尺寸。
 */
void HalVideoReadRectPhys(UINT32 Xp, UINT32 Yp, UINT32 Wp, UINT32 Hp, UINT32 *Out);
void HalVideoWriteRectPhys(UINT32 Xp, UINT32 Yp, UINT32 Wp, UINT32 Hp, const UINT32 *In);
void HalVideoClearScreen(UINT32 Color);

void HalVideoDrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color);
void HalVideoDrawCodepointAt(UINT32 X, UINT32 Y, UINT32 Cp, UINT32 Color);
void HalVideoDrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color);
void HalVideoDrawChar(char C, UINT32 Color);
void HalVideoDrawString(const char *Text, UINT32 Color);
void HalVideoEraseLastChar(void);

void HalVideoSetClipRegion(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background);
void HalVideoSetClipOrigin(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background);
void HalVideoGetTextCursor(UINT32 *X, UINT32 *Y);
void HalVideoSetTextCursor(UINT32 X, UINT32 Y);
void HalVideoClearClip(void);

/* PR-I2：焦点客户区按行滚动（Shell 滚轮） */
void HalVideoScrollClipLines(int Delta);

#endif
