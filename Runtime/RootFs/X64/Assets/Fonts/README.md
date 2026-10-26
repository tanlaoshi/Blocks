# Assets/Fonts — 运行时字体（点阵 PR-T3 + TTF gui-cjk）

## TTF（cjk-0 钉死；文件名跟字体）

Guest 路径：

| 文件 | 字体 | 用途 | 许可证（概要） |
| --- | --- | --- | --- |
| **`UMING.TTF`** | **AR PL UMing CN** | **UI 默认**（宋体向） | Arphic Public License |
| **`DROID.TTF`** | **Droid Sans Fallback** | 设置可选第二中文（黑体向） | Apache-2.0 |
| **`MONO.TTF`** | DejaVu Sans Mono | 终端等宽 Latin（现网壳默认不激活） | DejaVu 自由许可 |

旧名兼容：`CJK.TTF`→UMing、`CJK2.TTF`→Droid（仅旧 SD；新镜像用上表名）。

内核：`TrueTypeLoad` → `UMING.TTF` / `DROID.TTF` / `MONO.TTF`。短名单见 `Archive/路线图1027.md` [cjk-0](#)。**禁止**用点阵 FNT 顶中文。

**CJK** = Chinese / Japanese / Korean（中日韩表意文字合称）；仓内用具体字体文件名，避免笼统 `CJK*.TTF`。

## 点阵 FNT（遗留 / 拉丁回落）

Guest 路径：`Assets/Fonts/*.FNT`。启动时（FS 就绪后）`FontLoadAssets`：

1. 优先加载样本 `VGA8X16.FNT`（内容为 Linux `font_sun8x16` 8×16，GPL-2.0）
2. 再扫描目录下其余 `*.FNT` / `*.fnt`（最多 `FONT_RUNTIME_MAX`=2 槽）

缺文件或坏包 → **仅用内建** Terminus（16×32 / ×2 / 10×18）。Shell：`font` / `font reload`。

**商店（PR-S3）**：`store install sun8` 将 `VGA8X16.FNT` 安装到本目录（覆盖同名样本，不产生第二字面）；安装后自动 reload。也可把 `VGA8X16.FNT` 直接放进镜像。

## TOYF v1（小端）

| 偏移 | 内容 |
|------|------|
| 0 | magic `TOYF` |
| 4 | `version=1`, `scale`, `char_spacing`, `line_spacing`（各 u8） |
| 8 | `width`, `height`, `first_char`, `glyph_count`（各 u16 LE） |
| 16 | `name[16]` NUL 填充 |
| 32 | 字形：`glyph_count × height × ceil(width/8)` 字节，MSB 在左 |

内建点阵仍在仓库 `Common/Fonts/*.c`；本目录不链入 `Kernel.elf`。详见 [`Archive/技术手册.md`](../../Archive/技术手册.md)。
