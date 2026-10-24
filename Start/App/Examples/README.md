# 样例

| 目录 | 产物 | 依赖 |
| --- | --- | --- |
| `Hello/` | `MYAPP.ELF` | libBlocks |
| `Gui/` | `MYGUI.ELF` | + libUi + libGraphics；`PKG.TXT` 已 `window=yes` |
| `Net/` | `MYNET.ELF` | + libNet；Guest 先 `lwip on` |

```bash
cd ../../Kernel && ./Tools/build-sdk.sh
cd - && make -C Hello && make -C Gui && make -C Net
```

部署见 [`../Deploy.md`](../Deploy.md)。Gui 务必：

```bash
mkdir -p Runtime/RootFs/X64/Apps/mygui
cp Examples/Gui/build/MYGUI.ELF Runtime/RootFs/X64/Apps/mygui/MYGUI.ELF
cp Examples/Gui/PKG.TXT Runtime/RootFs/X64/Apps/mygui/PKG.TXT
# Guest: exec Apps/mygui/MYGUI.ELF
```
