# 独立 GUI 库 — PR 拆分

> 状态：待看。还没改代码。
>
> 目标：从现有 `Services/Ui` 几何层抽出一份别人也能用的纯 C 库。调用方只注册一个画点函数（可选再注册整块填色），库负责线、矩形、圆、圆角。Blocks 桌面继续用原来的函数名。
>
> 形状对标之前说的 uGUI / GuiLite：不依赖 HAL、Theme、Font、帧缓冲布局。

## 和驱动边界柱的关系

[驱动边界清理-PR拆分.md](驱动边界清理-PR拆分.md) 只管驱动的 `#include` 和互相点名。画点库、Match 表、XHCI 键盘搬家、Iwl EAPOL 搬走都不进那一柱，原因在那篇的「另立柱」节。本篇只开画点库这一柱。

## 现在的代码

| 文件 | 现状 |
| ---- | ---- |
| `Services/Ui/UI.c` | 线、矩形、圆、圆角、三角。线/圆已走 `HalVideoDrawPixel`；`UiFillRectangle` 直接调 `HalVideoFillRect`；圆角填充逐点画 |
| `Services/Ui/UIDraw.c` | 按钮、进度条。还包含 `HalVideo.h`、`Theme.h`、`Font.h` |
| `Services/Ui/UILayout.c` | 列表行、滚动条、命中测试。同样绑 Theme / Font |
| `Include/Library/UI.h` | 几何 + 控件声明挤在一个头里 |

桌面、文件窗、任务栏已经在调 `UiDrawLine` / `UiFillRectangle` 等。抽库时函数名可以继续叫 `Ui*`，也可以先落 `Gfx*` 再薄包装；以「调用方改动最少」为准，刀里写死。

## 读完应有的样子

```text
Library/Gfx/          # 或 Library/ 下若干 Gfx*.c；头在 Include/Library/
  GfxTypes.h          # 仅整数类型（可复用/复制 BootTypes 的 UINT*）
  Gfx.h               # 注册画点 + 线/矩形/圆/圆角
  Gfx.c               # 实现；无 HalVideo / Theme / Font
```

```c
void GfxSetPixel(void (*DrawPixel)(UINT32 X, UINT32 Y, UINT32 Color));
/* 可选：大矩形加速；不注册则用画点循环 */
void GfxSetFillRect(void (*FillRect)(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Color));

void GfxDrawLine(...);
void GfxDrawRectangle(...);
void GfxFillRectangle(...);
void GfxDrawCircle(...);
void GfxFillCircle(...);
void GfxDrawRoundRectangle(...);
void GfxFillRoundRectangle(...);
```

Blocks 开机或 Video 就绪时：

```c
GfxSetPixel(HalVideoDrawPixel);
GfxSetFillRect(HalVideoFillRect);   /* 可选 */
```

别的项目只实现自己的画点，即可链这份 `.c` / `.a`。未注册时几何函数空操作，不崩。

## 明确不做（本柱）

- 不把按钮、列表、滚动条、换行文字放进对外库。它们要 Theme 色和 Font 度量，属于 Blocks 的 `Services/Ui`。
- 不把窗口管理、桌面、文件窗搬进库。
- 不在本柱做 TTF / 点阵字库对外发布（字模许可另议）。
- 不改成单文件 header-only（可以后做；本柱先可独立编译的 `.c` + `.h`）。
- 不强制改用户态 `ToyGfx`；那是 Ring3 另一套，本柱只动内核侧几何。

## 刀

| 序 | PR | 改哪些 | 本刀不碰 | 验收 |
| --- | --- | --- | --- | --- |
| 1 | **PR-GFX-contract** | 新增 `Include/Library/Gfx.h`（或等价名）：画点/可选 FillRect 注册 + 几何 API 声明。`Library/Gfx/Gfx.c` 骨架：注册槽 + 未注册时空操作；把 `UI.c` 里线/矩形/圆/圆角/三角的算法搬进来，内部只调注册的画点（FillRect 优先走可选加速）。`UI.c` 改为薄包装调用 `Gfx*`，或 `#define UiDrawLine GfxDrawLine` 一类，保持现有调用方零改 | `UIDraw.c`、`UILayout.c`、Theme、Font、窗口 | `build.sh` + `smoke-boot.sh`。桌面边框/按钮轮廓与搬前视觉一致。`Library/Gfx` 无 `HalVideo` / `Theme` / `Font` 字符串 |
| 2 | **PR-GFX-wire** | 在 Video 初始化路径（现有 `VideoInit` / 等价处）调用 `GfxSetPixel(HalVideoDrawPixel)` 与可选 `GfxSetFillRect(HalVideoFillRect)`。确认开机早期若有人调几何、尚未注册，不写坏内存 | 控件绘制逻辑 | `build.sh` + `smoke-boot.sh`。串口无新增 panic |
| 3 | **PR-GFX-alpha** | `UiFillRectangleAlpha` / `UiBlendRgb`：若仍绑 `HalVideoBlend*`，改为可选第三钩子 `GfxSetBlend*`，或本柱声明 alpha 仍属 Blocks 胶水、几何库不含半透明。刀里二选一写死，默认：**半透明留在 Services，不进对外库** | 桌面半透明特效观感 | `build.sh`；半透明路径不回归或明确标为未进库 |
| 4 | **PR-GFX-headers** | `UI.h` 拆成：对外可拷的 `Gfx.h`（几何）+ Blocks 专用 `UiWidgets.h`（按钮/列表/滚动条，仍在 `Include/Services` 或 `Include/Library`）。调用几何的文件改 include；控件文件改 include widgets 头 | 控件实现体 | `build.sh`。grep：`Services/Ui/UI.c` 若已空则可删或只留兼容转发 |
| 5 | **PR-GFX-readme** | `Library/Gfx/README.md`：如何注册画点、最小示例、与 Blocks 的关系、许可（待你定）。`Kernel/README.md` 或 `同步.md` 旁挂一句「几何库在 Library/Gfx」 | 算法 | 文档可照着在另一个空工程里只链 Gfx.c + 假画点编过 |

## 建议顺序

```text
PR-GFX-contract   # 算法搬家 + Ui 薄包装
  → PR-GFX-wire   # 开机接上 HalVideoDrawPixel
  → PR-GFX-alpha  # 半透明进不进库，写死
  → PR-GFX-headers
  → PR-GFX-readme
```

## 做完后还在 Services 的

按钮、进度条、列表行、滚动条、UTF-8 换行仍在 `Services/Ui/`，继续用 Theme 和 Font。它们可以调 `GfxFillRectangle` 画底，但不是第三方库的一部分。

以后若要把控件也做成「即时模式 + 调用方提供文字宽度」，另开柱，不并进本五刀。
