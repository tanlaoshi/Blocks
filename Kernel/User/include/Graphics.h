/*
 * Graphics.h — 用户态绘图薄库（libGraphics；旧名 ToyGfx/Gfx）
 *
 * ABI：改签名 / 删符号须递增 GRAPHICS_ABI_VERSION_MAJOR。
 * 1.1.0：+ GraphicsDamageRect（SYS_GRAPHICS_DAMAGE_RECT 像素 blit）。
 * 1.2.0：+ 点 / 线 / 填充矩形 / 矩形框（底层仍 DamageRect；单次 ≤64×64）。
 * 1.3.0：+ GraphicsInitialize / GraphicsSystemFontRoot（方案 C：系统字体约定）。
 */
#ifndef USER_GRAPHICS_H
#define USER_GRAPHICS_H

#include <unistd.h>
#include "GraphicsDamage.h"

#define GRAPHICS_ABI_VERSION_MAJOR 3
#define GRAPHICS_ABI_VERSION_MINOR 0
#define GRAPHICS_ABI_VERSION_PATCH 0
#define GRAPHICS_ABI_VERSION_STRING "3.0.0"

/* 0x00RRGGBB，与内核 UI 色值习惯一致 */
#define GRAPHICS_COLOR_BLACK       0x00000000u
#define GRAPHICS_COLOR_WHITE       0x00FFFFFFu
#define GRAPHICS_COLOR_LIGHT_GRAY  0x00C0C0C0u

/* 与内核 DAMAGE_RECT_MAX_PX 对齐：单次最多 64×64 */
#define GRAPHICS_DAMAGE_RECT_MAX_PIXELS 4096u

/* 系统共享字库根（TOYOS 卷相对路径；应用勿拼私有路径） */
#define GRAPHICS_SYSTEM_FONT_ROOT "Assets/Fonts"

/*
 * GraphicsInitialize — 声明使用系统字体约定（PR-S-bundle-font）
 *
 * 内核启动已加载 Assets/Fonts 下 .FNT；本调用无加载动作，成功恒 0。
 * 若 PKG 写了 font=，ProcessExec 会由内核挂私有字（PR-S-app-font）；
 * Guest 仍只调本 API + DamageText，勿自行拼路径。
 */
int GraphicsInitialize(void);

/* 返回系统字库根字符串（静态常量，勿 free） */
const char *GraphicsSystemFontRoot(void);

/*
 * GraphicsDamageText — 在用户窗客户区显示一行文字（内核当前选中的系统字体）
 * 成功 0；失败 -1（无效 wid / 空指针 / 内核拒绝）
 */
int GraphicsDamageText(int WindowId, const char *Text);

/*
 * GraphicsDamageRect — 客户区相对坐标像素矩形 blit（PR-G-desk-3）
 * 成功 0；失败 -1（越界 / 过大 / 空指针 / 内核拒绝）
 */
int GraphicsDamageRect(int WindowId, const GRAPHICS_DAMAGE_RECT *Desc);

/* PR-A-gfx-api：点 / 线 / 矩形。大矩形按 ≤64×64 分块。成功 0，失败 -1。 */
int GraphicsDrawPixel(int WindowId, unsigned X, unsigned Y, unsigned Color);
int GraphicsDrawLine(int WindowId, int X0, int Y0, int X1, int Y1, unsigned Color);
int GraphicsFillRect(int WindowId, unsigned X, unsigned Y, unsigned W, unsigned H,
                   unsigned Color);
int GraphicsDrawRect(int WindowId, unsigned X, unsigned Y, unsigned W, unsigned H,
                   unsigned Color);

#endif
