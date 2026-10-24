# Start / App — 毛坯房

不懂内核也能开干。工具链在 `Kernel/Build/BlocksSDK`（先打一次包）；手册与样例在本目录。

## 三步

```bash
# 0）树内一次：打出可链的 SDK（头 + lib + ld）
cd ../../Kernel && ./Tools/build-sdk.sh

# 1）复制毛坯（可拷到仓外；仓外须 make BLOCKS_SDK=…）
cp -a Template MyFirstApp
cd MyFirstApp

# 2）改 main.c；加 .c 就改 Makefile 的 SRCS
make                             # → build/MYAPP.ELF

# 3）部署进 Blocks：见 Deploy.md
```

不拷走、直接试编毛坯：

```bash
make -C Template && make -C Template clean
```

## 本目录

| 路径 | 内容 |
| --- | --- |
| [`Template/`](Template/) | 毛坯：`main.c` + `Makefile` + `PKG.TXT` |
| [`Examples/`](Examples/) | 可跑样例：Hello / Gui / Net |
| [`Docs/`](Docs/) | 自包含手册（红线 / 身份 / Guide / API / ABI） |
| [`Deploy.md`](Deploy.md) | 编完怎么进 Guest / 商店 |

开窗 App：**必须** `Apps/<id>/ELF` + `PKG.TXT` 且 **`window=yes`**（见 Deploy.md）。  
扁平 `Apps/FOO.ELF` 会 CoopDrain 钉死桌面——不是「关窗偶发」，是部署契约错了。
