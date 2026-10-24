# BlocksSDK 示例

| 目录 | 产物 | 依赖 |
| --- | --- | --- |
| `Hello/` | `MYAPP.ELF` | CRT / libBlocks |
| `Gui/` | `MYGUI.ELF` | + libUi + libGraphics |
| `Net/` | `MYNET.ELF` | + libNet（Guest：`lwip on`） |

```bash
make -C Hello
make -C Gui
make -C Net
```

ELF 拷到 RootFs 后 `exec …`（FAT 8.3 大写）。

注意：`exec MYAPP.ELF` 若卷根没有该文件，会落到 **`Class/MYAPP.ELF`**。仓库同步**不会**预装课外模板名；勿留旧 ToyUi 的 `Class/MYAPP.ELF`（会整屏 Present 饿死鼠标）。推荐：

```bash
cp Examples/Hello/build/MYAPP.ELF /path/to/Runtime/RootFs/X64/MYAPP.ELF
# 再在 Guest：exec MYAPP.ELF
```
