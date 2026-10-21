/*
 * Font.h — 点阵字体抽象（PR-D1）
 *
 * 字形数据在 Library/Fonts/；绘制层（Video/Console）只经本 API，不直接 include 某份点阵表。
 * 加字体：Library/Fonts/ 新增数据 + FontRegistry 注册一行。汉字点阵是 18×18。
 */
#ifndef FONT_H
#define FONT_H

#include "BootTypes.h"

typedef struct FONT_FACE {
    const char *Name;
    UINT32      Width;          /* 字形像素宽 */
    UINT32      Height;         /* 字形像素高 */
    UINT32      BytesPerGlyph;
    UINT32      BytesPerRow;
    UINT32      CharSpacing;    /* 字间距（逻辑像素，再乘 Scale） */
    UINT32      LineSpacing;    /* 行间距 */
    UINT32      Scale;          /* 像素放大倍数，至少 1 */
    const UINT8 *Glyphs;        /* FirstChar 起连续 GlyphCount 个字形 */
    UINT32      GlyphCount;
    UINT32      FirstChar;      /* 通常 32 */
} FONT_FACE;

/* Terminus 10×18 独立点阵（Library/Fonts/terminus10x18.c） */
extern const FONT_FACE gFontFaceTerminus10x18;

void FontInitialize(void);
/*
 * FS 就绪后扫描 Assets/Fonts 下的 FNT。有文件就只用这些；一个都没有才留内建。
 * 成功加载至少一包返回 0，否则 -1。
 */
int FontLoadAssets(void);
/* 按字面名字找 id（大小写不敏感）。没有返回 -1。 */
int FontIdByName(const char *Name);
int FontReloadAssets(void);
/* PR-UI-ttf-0：读 CJK.TTF 校验 sfnt；不绘制。缺文件返回 -1 */
int FontTtfLoad(void);
const UINT8 *FontTtfBlob(UINT32 *OutSize);
const UINT8 *FontTtfMonoBlob(UINT32 *OutSize);
int FontTtfInit(void);
int FontTtfMonoInit(void);
/* 1=stbtt 已 Init（可栅格）；0=未加载/无 FPU */
int FontTtfReady(void);
int FontTtfMonoReady(void);
/* Shell/TTY：嵌套计数；激活时 ASCII 走 MONO.TTF */
void FontTtfMonoBegin(void);
void FontTtfMonoEnd(void);
int FontTtfMonoActive(void);
UINT32 FontTtfMonoAdvance(void);
/* mono 槽 Lookup+同步栅（控制台路径）；未就绪 0 */
const UINT8 *FontTtfMonoCacheGet(UINT32 Cp, UINT32 *OutW, UINT32 *OutH);
/* 汉字绘制高度。至少 24，不超过缓存的 32。英文按点阵原样画，不跟着拉高。 */
UINT32 FontTtfPixel(void);
int FontTtfRasterCp(UINT32 Cp, UINT8 *Pix18);
/* FreeType 挂钩：Init 在 TTF Load/Init 之后；Raster 失败由 RasterCp 软退 stb */
int FontFtInit(void);
int FontFtReady(void);
int FontFtRasterCp(UINT32 Cp, UINT8 *Pix, UINT32 CellW, UINT32 CellH);
/* 最近一次 FontFtRasterCp 成功的像素步进；未走 FT / 已 Clear 为 0 */
UINT32 FontFtLastAdvance(void);
void FontFtClearLastAdvance(void);
const UINT8 *FontTtfCacheGet(UINT32 Cp, UINT32 *OutW, UINT32 *OutH);
/* HIT 槽步进；未命中 0。拉丁约为 Side/2，汉字 Side */
UINT32 FontTtfCacheAdvance(UINT32 Cp);
/* 1=已栅格失败（真缺字）；可回落 cjk32 */
int FontTtfCacheMissed(UINT32 Cp);
/* 不在点阵里：x86 已排队等栅格则 1。队列满或已确认没有字形则 0。 */
int FontTtfCachePending(UINT32 Cp);
/* PR-UI-ttf-3：仅 Worker / 开窗短 burst；绘制路径 FontTtfCacheGet 只 Lookup */
void FontTtfPreheatUtf8(const char *S);
/* Worker：泵绘制路径排队的缺字；1=仍有排队 */
int FontTtfWantStep(void);
/* 开窗前/Worker：一次抽干最多 Max 个 Want（0→32） */
UINT32 FontTtfWantDrain(UINT32 Max);
/* 自上次调用以来有新的 TTF 字形进缓存则 1。给界面把点阵补位换成真字。 */
/* 1=有新 HIT 待合成；Ack 前可反复查。合成后 Ack。 */
int FontTtfTakePaint(void);
void FontTtfAckPaint(void);
/* 预热整批结束后喊一声，Gui 再合成一次 */
void FontTtfBumpPaint(void);
/*
 * PR-S-app-font：按路径加载 TOYF 到专用应用槽（覆盖上次私有字）。
 * 成功返回字体 id（≥0）；失败 -1。不改当前选中 id。
 */
int FontLoadPath(const char *Path);
/* 卸应用私有槽；当前 id 若失效则钳到合法值 */
void FontUnloadApp(void);
UINT32 FontCount(void);
UINT32 FontCurrentId(void);
const FONT_FACE *FontGetById(UINT32 Id);
const FONT_FACE *FontGetCurrent(void);
/* 成功返回 0；Id 越界返回 -1 且保持当前字体 */
int FontSetById(UINT32 Id);

/* 当前字体度量（Scale 已计入） */
UINT32 FontCellW(void);
UINT32 FontCellH(void);
UINT32 FontAdvanceX(void);
UINT32 FontAdvanceY(void);
/* 可打印 ASCII 返回字形指针；否则 NULL */
const UINT8 *FontGlyph(char C);

/*
 * UTF-8：解析一个码点，返回消费字节数；非法序列返回 0。
 * 汉字等宽字形见 FontCjk32Lookup / FontGlyphCp。
 */
UINTN Utf8Decode(const char *S, UINT32 *OutCp);
/* ASCII→Terminus；汉字先 TTF 缓存，再 TOYF 文件字，再点阵 */
const UINT8 *FontGlyphCp(UINT32 Cp, UINT32 *OutW, UINT32 *OutH);
/* 上一次 FontGlyphCp 返回空，是因为 x86 还在等 TTF，这一帧先不画框。 */
int FontGlyphCpDeferred(void);
/* 最近一次 FontGlyphCp 的位深（ASCII=1，TTF=8，cjk32=4） */
UINT32 FontGlyphCpBpp(void);
const UINT8 *FontCjk32Lookup(UINT32 Cp, UINT32 *OutW, UINT32 *OutH);
const UINT8 *FontCjk16Lookup(UINT32 Cp, UINT32 *OutW, UINT32 *OutH);
/* PR-UI-cjk-gray：CJK 点阵位深（1 或 4）；ASCII 仍 1bpp */
UINT32 FontCjkBitsPerPixel(void);
/* 当前 CJK 原生边长（与 gen --dim 一致） */
UINT32 FontCjkDim(void);
/* 仅 Face->Scale；不再为凑行高整数拉高字形 */
UINT32 FontGlyphStretch(UINT32 GlyphH);
/* 乘上字号后，高于当前行则宽高一起收到行高 */
void FontGlyphBox(UINT32 GlyphW, UINT32 GlyphH, UINT32 *OutW, UINT32 *OutH);
/* UTF-8 字符串像素宽（含汉字前进） */
UINT32 FontCodepointAdvance(UINT32 Cp);
UINT32 FontStringWidth(const char *S);

#endif
