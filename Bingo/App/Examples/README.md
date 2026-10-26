# 样例

课外 SDK 样例（**非** Class 课设源）。课设 / Store / Class 政策见 [`Kernel/User/Apps/README.md`](../../../Kernel/User/Apps/README.md)。

先打 SDK：`cd ../../../Kernel && ./Tools/build-sdk.sh`

| 目录 | 产物 | 依赖 | 备注 |
| --- | --- | --- | --- |
| `Hello/` | `MYAPP.ELF` | libBlocks | |
| `Gui/` | `MYGUI.ELF` | + libUi + libGraphics | `PKG.TXT` 已 `window=yes` |
| `Net/` | `MYNET.ELF` | + libNet | Guest 先 `lwip on` |
| `Batch/` | `BATCH.ELF` | + Ui + Graphics | |
| `GuiImmediate/` | `GUIIM.ELF` | + Ui + Graphics | |
| `Pages/` | `PAGES.ELF` | + Ui + Graphics | 多页 |
| `Style/` | `STYLE.ELF` | + Ui + Graphics | |
| `UiText/` | `MYAPP.ELF` | 树内 Kernel + `uitxt2h` | 用 `BlocksUser.mk`；**工具暂缺仓内**则本例不可编 |
| `UiDesigner/` | `MYAPP.ELF` | 同上 | 同上 |

```bash
make -C Hello && make -C Gui && make -C Net
make -C Batch && make -C GuiImmediate && make -C Pages && make -C Style
# UiText / UiDesigner：待 Tools/UiLayout/uitxt2h.py 回仓后再编
```

部署见 [`../Deploy.md`](../Deploy.md)。Gui 务必：

```bash
mkdir -p Runtime/RootFs/X64/Apps/mygui
cp Examples/Gui/build/MYGUI.ELF Runtime/RootFs/X64/Apps/mygui/MYGUI.ELF
cp Examples/Gui/PKG.TXT Runtime/RootFs/X64/Apps/mygui/PKG.TXT
# Guest: exec Apps/mygui/MYGUI.ELF
```
