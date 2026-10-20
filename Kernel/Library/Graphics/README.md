# 几何库（Graphics）

纯 C。画线、矩形、圆、圆角和三角。不依赖帧缓冲、主题或字体。

Blocks 桌面仍调用 `UiDrawLine` 等名字（`Services/Ui` 薄包装）。开机在 `VideoInitialize` 里把 `HalVideoDrawPixel` / `HalVideoFillRect` 登记进来。半透明、按钮、列表和滚动条不在本库。

| | 路径 |
| --- | --- |
| 头 | `Kernel/Include/Library/Graphics.h` |
| 实现 | `Kernel/Library/Graphics/Graphics.c` |
| 类型 | `Include/Abi/BootTypes.h` |

勿与 `User/Library/Graphics`（用户态 libGraphics / syscall）混淆。旧名 `Library/Gfx` 已删除。

许可还没定。

## 登记画点

```c
void GraphicsSetPixel(void (*DrawPixel)(UINT32 X, UINT32 Y, UINT32 Color));
void GraphicsSetFillRect(void (*FillRect)(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color));
```

未登记画点时几何函数直接返回。未登记 `GraphicsSetFillRect` 时，实心矩形用画点逐点填。

## 最小示例

```c
#include "Graphics.h"

static void Plot(UINT32 X, UINT32 Y, UINT32 Color) {
    (void)X;
    (void)Y;
    (void)Color;
}

int main(void) {
    GraphicsSetPixel(Plot);
    GraphicsDrawLine(0, 0, 8, 4, 0x00FFFFFFu);
    return 0;
}
```

```bash
gcc -std=c11 -Wall -Wextra -I. -c Graphics.c -o Graphics.o
gcc -std=c11 -Wall -Wextra -I. -c main.c -o main.o
gcc -o graphics-try main.o Graphics.o
```
