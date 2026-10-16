# 几何库

纯 C。画线、矩形、圆、圆角和三角。不依赖帧缓冲、主题或字体。

Blocks 的桌面仍调用 `UiDrawLine` 这些名字，包装在 `Services/Ui/UI.c`。开机在 `InitializeVideo` 里把 `HalVideoDrawPixel` 和 `HalVideoFillRect` 登记进来。半透明、按钮、列表和滚动条不在这个库里。

头文件：`Kernel/Include/Library/Gfx.h`。实现：`Kernel/Library/Gfx/Gfx.c`。类型来自 `Include/Abi/BootTypes.h`。

许可还没定。

## 登记画点

```c
void GfxSetPixel(void (*DrawPixel)(UINT32 X, UINT32 Y, UINT32 Color));
void GfxSetFillRect(void (*FillRect)(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color));
```

没登记画点时，几何函数直接返回，不写内存。没登记 `GfxSetFillRect` 时，实心矩形用画点逐点填。

## 最小示例

另建空目录，只编译这两个文件加一个假画点。把仓库里的 `Gfx.h`、`Gfx.c`、`BootTypes.h` 放到编译器能找到的路径。

```c
#include "Gfx.h"

static void Plot(UINT32 X, UINT32 Y, UINT32 Color) {
    (void)X;
    (void)Y;
    (void)Color;
}

int main(void) {
    GfxSetPixel(Plot);
    GfxDrawLine(0, 0, 8, 4, 0x00FFFFFFu);
    return 0;
}
```

```bash
gcc -std=c11 -Wall -Wextra -I. -c Gfx.c -o Gfx.o
gcc -std=c11 -Wall -Wextra -I. -c main.c -o main.o
gcc -o gfx-try main.o Gfx.o
```
