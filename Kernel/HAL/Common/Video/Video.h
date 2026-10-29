/*
 * Video.h — GOP 帧缓冲驱动（仅 HAL 内部使用，Common 请用 HalVideo.h）
 */
#ifndef VIDEO_H
#define VIDEO_H

#include "BootHandoff.h"

/* 当前屏幕状态（分辨率、帧缓冲地址、文本光标） */
typedef struct {
    UINT32 Width;
    UINT32 Height;
    UINT32 PixelsPerScanLine;
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
    UINT32 CursorX;
    UINT32 CursorY;
} SCREEN_INFO;

void VideoSet(VIDEO_CONFIG *VideoConfig);
/* 1=写 FB 时对调 R/B（Pi mailbox RGB scanout vs 0x00RRGGBB） */
void VideoSetFbSwapRb(int On);
/* PR-G9：启用后缓冲；Buf 为 gDrawW*gDrawH 个 UINT32（=物理尺寸，见 VideoGetDrawSize），Pages 供记录 */
void VideoSetBackbuffer(UINT32 *Buf, UINT32 Pages);
/* PR-G-hotres：释放后缓冲页（切分辨率前调用） */
void VideoReleaseBackbuffer(void);
int VideoBackbufferEnabled(void);
UINT64 VideoBackbufferBase(void);
UINT32 VideoBackbufferPages(void);
/* 脏矩形 blit 到 GOP；无后缓冲时为空操作 */
void VideoPresent(void);
/* 只提交光标脏（小框、LivePatch）；Restore/Paint 各调一次，勿并 AABB */
void VideoPresentCursor(void);
/* 循环 Present 直到脏区清空（4K 全屏合成勿半截留下任务栏空洞） */
void VideoPresentFlush(void);
/* 丢掉脏区（不 Present）；Boot 已铺同色底时只填后缓冲用 */
void VideoDiscardDirty(void);
void DirtyUnion(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
/* Rows=0 → 默认 64；拖窗可传极大值整块 blit */
void VideoSetPresentChunkRows(UINT32 Rows);
/* 热切（Bochs/GOP）→ HAL/X64/Drivers/Video/Hotres/；不进本头 */
UINT64 VideoFrameBufferBase(void);
UINT64 VideoFrameBufferSize(void);
/* GPU 翻页后切换 CPU 所见 scanout（与 PLANE_SURF 一致） */
void VideoSetScanout(UINT32 *Va, UINT32 PitchPx, UINT64 Phys, UINT64 Size);
/* 直写 scanout（boot 进度，勿与 Present 混用长路径） */
/* PR-GUI-l2-font：1=点阵边缘灰度；0=硬 1bpp */
void VideoSetGlyphSmooth(int On);
void VideoDrawBeginFront(void);
void VideoDrawEndFront(void);
void VideoGetSize(UINT32 *Width, UINT32 *Height);
/* PR-23E：后缓冲绘制尺寸 —— 恒为物理分辨率（任何 UI 比例） */
void VideoGetDrawSize(UINT32 *Width, UINT32 *Height);
/* UI 整体缩放 50/100/150/200；逻辑分辨率见 GetSize，物理见 GetPhysicalSize */
UINT32 VideoGetUiScale(void);
/*
 * PR-23E：绘制比例 = gUiScale/100 的有理数形式（50%→1/2、100%→1/1、
 * 150%→3/2、200%→2/1）。字体层按 FontTtfPixel()*Num/Den 栅格物理位图，
 * 布局/行高仍用逻辑单位，故必须只读、不可反向改布局。
 * 除法一律 (UINT64)v * Num / Den，禁浮点。
 */
UINT32 VideoGetDrawNum(void);
UINT32 VideoGetDrawDen(void);
/*
 * 兼容旧接口：一个逻辑像素最多占几个物理像素，即 ScaleW(1)
 * （100%/50%→1，150%/200%→2）。只能当容量上限，不能拿来做坐标换算。
 */
UINT32 VideoGetDrawScale(void);
void VideoGetPhysicalSize(UINT32 *Width, UINT32 *Height);
int VideoSetUiScale(UINT32 Percent);
void VideoDrawPixel(UINT32 X, UINT32 Y, UINT32 Color);
/* 忽略文字 clip，供光标等全屏叠层 */
void VideoDrawPixelRaw(UINT32 X, UINT32 Y, UINT32 Color);
void VideoXorPixelRaw(UINT32 X, UINT32 Y, UINT32 Mask);
/* 光标叠层：后续 DrawPixelRaw/WriteRect 记入光标脏区（勿并进内容 AABB） */
void VideoCursorOverlayBegin(void);
void VideoCursorOverlayEnd(void);
UINT32 VideoReadPixel(UINT32 X, UINT32 Y);
void VideoFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color);
/* PR-GUI-alpha：Src over Dst，Alpha=0..255；写后缓冲 + DirtyUnion */
UINT32 VideoBlendRgb(UINT32 Dst, UINT32 Src, UINT8 Alpha);
void VideoBlendPixel(UINT32 X, UINT32 Y, UINT32 Color, UINT8 Alpha);
void VideoBlendPixelRaw(UINT32 X, UINT32 Y, UINT32 Color, UINT8 Alpha);
/*
 * PR-23b：物理像素直写（坐标已是物理像素，不再乘绘制倍数）。
 * 只给「已按物理像素栅格」的 TTF 位图用；裁剪仍按所属逻辑像素判定。
 */
void VideoDrawPixelPhys(UINT32 Xp, UINT32 Yp, UINT32 Color);
UINT32 VideoReadPixelPhys(UINT32 Xp, UINT32 Yp);
void VideoBlendPixelPhys(UINT32 Xp, UINT32 Yp, UINT32 Color, UINT8 Alpha);
void VideoBlendFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                        UINT32 Color, UINT8 Alpha);
void VideoCopyRect(UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                   UINT32 Width, UINT32 Height);
void VideoReadRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 *Out);
void VideoWriteRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT32 *In);
/*
 * PR-23c：物理像素矩形读写（save-under 专用）。
 * 坐标/尺寸都是物理像素（调用方自己按 Num/Den 换算），不再按逻辑块采样；
 * 行距 = DrawPitch，Out/In 按 Wp 紧排。越界裁剪到 gDrawW/gDrawH。
 */
void VideoReadRectPhys(UINT32 Xp, UINT32 Yp, UINT32 Wp, UINT32 Hp, UINT32 *Out);
void VideoWriteRectPhys(UINT32 Xp, UINT32 Yp, UINT32 Wp, UINT32 Hp, const UINT32 *In);
void VideoClearScreen(UINT32 Color);
void VideoDrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color);
void VideoDrawCodepointAt(UINT32 X, UINT32 Y, UINT32 Cp, UINT32 Color);
void VideoDrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color);
void VideoDrawChar(char c, UINT32 Color);
void VideoDrawCodepoint(UINT32 Cp, UINT32 Color);
void VideoDrawString(const char *Text, UINT32 Color);
void VideoNewLine(void);
void VideoEraseLastChar(void);
void VideoSetClipRegion(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background);
void VideoSetClipOrigin(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background);
void VideoGetTextCursor(UINT32 *X, UINT32 *Y);
void VideoSetTextCursor(UINT32 X, UINT32 Y);
void VideoClearClip(void);
/* PR-I2：客户区滚轮。Delta>0 内容下移（看上方）；Delta<0 内容上移 */
void VideoScrollClipLines(int Delta);

#endif
