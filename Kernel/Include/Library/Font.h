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
/* cjk-4：TTF blob/Init 后重建表（UI TTF 进 id0）；设置列表标签 */
void FontNotifyTtfReady(void);
void FontFaceLabel(UINT32 Id, char *Out, int OutMax);
/* PR-UI-ttf-0：读 UMING.TTF / DROID.TTF（旧名 CJK*.TTF 兼容）；缺主文件 -1 */
#define FONT_TTF_UI_MAX 2u
int FontTtfLoad(void);
const UINT8 *FontTtfBlob(UINT32 *OutSize); /* 主槽 UMING.TTF */
const UINT8 *FontTtfUiBlob(UINT32 Slot, UINT32 *OutSize);
UINT32 FontTtfUiCount(void);
/* 当前选中的 UI TTF 槽（切 UMing/Droid 时改；Latin 项保持上次） */
UINT32 FontTtfUiActiveSlot(void);
/* 1=当前字体表项是 UI TTF（UMing/Droid）；ASCII 走同文件拉丁 */
int FontUiTtfSelected(void);
/* 1=最近一次 FontGlyphCp 交出的是 UI TTF 拉丁灰图（绘制用软 AA，勿汉字硬 crisp） */
int FontGlyphCrispLatin(void);
const UINT8 *FontTtfMonoBlob(UINT32 *OutSize);
int FontTtfInit(void);
int FontTtfMonoInit(void);
/* 1=stbtt 已 Init（可栅格）；0=未加载/无 FPU */
int FontTtfReady(void);
int FontTtfMonoReady(void);
/* MONO.TTF 嵌套门面（保留）；壳/TTY 现与桌面同 Latin 点阵，默认不激活 */
void FontTtfMonoBegin(void);
void FontTtfMonoEnd(void);
int FontTtfMonoActive(void);
UINT32 FontTtfMonoAdvance(void);
/* mono 槽 Lookup+同步栅；未就绪 0 */
const UINT8 *FontTtfMonoCacheGet(UINT32 Cp, UINT32 *OutW, UINT32 *OutH);
/* 汉字绘制高度。至少 24，不超过缓存的 32。英文按点阵原样画，不跟着拉高。 */
UINT32 FontTtfPixel(void);
int FontTtfRasterCp(UINT32 Cp, UINT8 *Pix18);
/* FreeType：Worker 懒启；Raster 未就绪/失败软退 stb */
int FontFtInit(void);
int FontFtReady(void);
/* 丢全部 HIT（慎用，易风暴） */
void FontTtfCacheDropHits(void);
/* 仅丢汉字 HIT，供 FT 就绪后升级锐化，不动拉丁槽 */
void FontTtfCacheDropCjkHits(void);
int FontFtRasterCp(UINT32 Cp, UINT8 *Pix, UINT32 CellW, UINT32 CellH);
/* 最近一次 FontFtRasterCp 成功的像素步进；未走 FT / 已 Clear 为 0 */
UINT32 FontFtLastAdvance(void);
void FontFtClearLastAdvance(void);
const UINT8 *FontTtfCacheGet(UINT32 Cp, UINT32 *OutW, UINT32 *OutH);
/* HIT 槽步进；未命中 0。拉丁约为 Side/2，汉字 Side */
UINT32 FontTtfCacheAdvance(UINT32 Cp);
/* 1=已栅格失败（真缺字）；绘制层走 U+FFFD/tofu，禁点阵顶中文 */
int FontTtfCacheMissed(UINT32 Cp);
/* 已排队等栅格则 1。队列满或已确认没有字形则 0。 */
int FontTtfCachePending(UINT32 Cp);
/* PR-UI-ttf-3：Preheat 可同步；WantDrain 仅 Worker（Gui 路径勿大 burst） */
void FontTtfPreheatUtf8(const char *S);
/* Worker：泵绘制路径排队的缺字；1=仍有排队 */
int FontTtfWantStep(void);
/* Worker：一次抽干最多 Max 个 Want（0→32）；有抽则 BumpPaint */
UINT32 FontTtfWantDrain(UINT32 Max);
/* 1=有新 HIT 待合成；Ack 前可反复查。合成后 Ack。 */
int FontTtfTakePaint(void);
void FontTtfAckPaint(void);
/* WantDrain/预热整批后喊一声；Gui 闲时 RequestRefresh */
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
/* ASCII 点阵 face：UI TTF 选中时仍回 Terminus（软退/度量兜底）；真同脸走 FontGlyphCp→TTF */
const FONT_FACE *FontGetLatinFace(void);
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
 * 汉字 GUI 只走 FontGlyphCp→TTF；FontCjk32Lookup 仅遗留/工具，勿顶 UI 中文。
 */
UINTN Utf8Decode(const char *S, UINT32 *OutCp);
/* UI TTF 选中：ASCII+汉字同文件 TTF；否则 ASCII→点阵，汉字只 TTF（Deferred；禁点阵顶中文） */
const UINT8 *FontGlyphCp(UINT32 Cp, UINT32 *OutW, UINT32 *OutH);
/* 上一次 FontGlyphCp 返回空：等 TTF Want 泵，本帧不画框/点阵。 */
int FontGlyphCpDeferred(void);
/* 最近一次 FontGlyphCp 的位深（ASCII=1，TTF=8） */
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
