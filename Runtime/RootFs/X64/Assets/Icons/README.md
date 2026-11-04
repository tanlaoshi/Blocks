# Blocks 桌面 / UI 图标

圆角矩形底板 + 白色几何字形；**每种桌面图标不同底板色**（Shell 钢蓝 / Files 绿 / Store 青 / …）便于分辨。  
BI_RGB 无透明：圆角外填纯黑 `0x000000`，桌面 blit 色键跳过；无方框描边。

**开始菜单留手**（勿用桌面新风格覆盖）：

- 任务栏开始钮：代码九宫格 `DrawStartMark`
- `START` / `POWER` / `REBOOT` / `GAME`：迁入原版
- `menu/SHELL|SET|FILES|STORE|INFO.BMP`：菜单行系统项原版；桌面槽用 `bmpNN/` 新图

## 来源与许可

- 字形为 Blocks 自绘几何（对齐 HotSend 应用图标调色），不再依赖 Lucide 栅格底板
- 旧 `svg/` / Lucide 声明仍保留作参考；运行时只读 BMP

## 多档尺寸（缩放 / 分辨率）

| `ThemeUiScale` | 目录 | 边长 |
|----------------|------|------|
| 50% | `bmp32/` | 32 |
| 100% | `bmp48/` | 48 |
| 150% | `bmp72/` | 72 |
| 200% | `bmp96/` | 96 |

逻辑桌面图标仍是 `DESKTOP_ICON_SIZE`（48）；绘制按 `HalVideoGetDrawNum/Den` **物理像素采样**，高档 BMP 在 150%/200% 上保持锐利。热切缩放档变时 Worker 重载对应目录。

| 路径 | 说明 |
|------|------|
| `bmp32/` `bmp48/` `bmp72/` `bmp96/` | 运行时按缩放选档 |
| `SHELL.BMP` 等（扁平） | 与 `bmp48/` 同内容；`LoadBmpPath` 首选 |
| `png32/` `png48/` | 预览（透明圆外） |
| `svg/` | 历史 Lucide 源（可选） |

### 文件名（FAT 友好）与 UI 映射

| 文件 | 用途 |
|------|------|
| `SHELL.BMP` | 桌面 Shell + 开始菜单项 |
| `SET.BMP` | Settings |
| `FILES.BMP` | Files |
| `STORE.BMP` | Store |
| `INFO.BMP` | Devices / Info |
| `START.BMP` | 任务栏「开始」钮 |
| `GAME.BMP` / `POWER.BMP` / `REBOOT.BMP` | 开始菜单 |
| `NET.BMP` / `HELP.BMP` / `HOME.BMP` / `CLOSE.BMP` | 预留 / 默认应用图标 |

运行时：`IconBitmapLoad.c` → `FileSystemReadFile` + `BmpDecode`；缺失回退色块。

壁纸仍在 [`../Images/WALL.BMP`](../Images/WALL.BMP)。

## Guest 资源

`prepare-rootfs.sh` / `prepare-virt-rootfs.sh` 将本目录同步到 Guest `Assets/Icons/`。  
`sync-usb --kernel-only` 会带上全部 `bmpNN/` 档。
