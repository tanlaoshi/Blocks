# 部署进 Blocks

编出 `build/*.ELF` 之后，按**是否开窗**选路径。选错会 CoopDrain 钉死桌面（关不了窗、下次 run 也像卡死）。

## 开窗 App（Gui / Chat / TaskMgr…）— 必用包目录

**禁止** `Apps/MYGUI.ELF` 这种扁平文件；`ExtractAppsId` 要 `Apps/<id>/…`。

```bash
# 仓根；以 Gui 样例为例
mkdir -p Runtime/RootFs/X64/Apps/mygui
cp Start/App/Examples/Gui/build/MYGUI.ELF Runtime/RootFs/X64/Apps/mygui/MYGUI.ELF
cp Start/App/Examples/Gui/PKG.TXT          Runtime/RootFs/X64/Apps/mygui/PKG.TXT
# PKG 必须含：window=yes
```

Guest：

```text
blocks> exec Apps/mygui/MYGUI.ELF
```

或经桌面/商店安装同结构包。

| 要件 | 为何 |
| --- | --- |
| `Apps/<id>/` + `PKG.TXT` | 认 Id、读 `window=` |
| **`window=yes`** | exec **异步**；否则 CoopDrain → 鼠/关窗全死 |
| ELF 8.3 大写 | FAT 习惯 |

内核护栏：CoopDrain 中 `UiCreateWindow` **直接失败**（串口有 `refuse create under CoopDrain`），不再默默开窗把机器钉死。

## 无窗 App（Hello / 算数…）

可放卷根或 `Class/`：

```bash
cp Start/App/Template/build/MYAPP.ELF Runtime/RootFs/X64/MYAPP.ELF
# Guest: exec MYAPP.ELF
```

`printf` 多半只上**串口**。

## 商店包（可选）

`Store/packages/<id>/`：`PKG.TXT` + `*.ELF`。字段与打包步骤见 [`Docs/Guide.md`](Docs/Guide.md)。

## 网络样例

Guest 先 `lwip on`。QEMU SLIRP 宿主机常见 `10.0.2.2`。

## 若已经钉死

宿主杀掉旧 QEMU 再 `run`；去掉扁平 `Apps/*.ELF`，改成上表包目录。
