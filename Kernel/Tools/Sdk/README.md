# BlocksSDK

开课用户态开发包（宿主交叉编译 → Guest `exec`）。**不**含内核源；解压即可 `make`。

**人手入口在仓库** [`Start/App/`](../../../Start/App/)（毛坯 / 样例 / 红线 / 部署）。本目录是打进 tar 的工具链与同源 Examples。

- 包号：见根目录 `VERSION`（开课冻结 `1.0.0-course`）
- CRT：头文件 `Blocks/version.h`（与仓库同步打进本包）

## 快速开始

```bash
tar xzf BlocksSDK.tar.gz
make -C BlocksSDK/Examples/Hello    # → Examples/Hello/build/MYAPP.ELF
```

自己的应用：

```bash
mkdir myapp && cp BlocksSDK/Makefile.template myapp/Makefile
# 编辑 myapp/main.c；Makefile 里 BLOCKS_SDK=…
make -C myapp
```

## 布局

| 路径 | 内容 |
| --- | --- |
| `Include/` | 用户态头（+ `Include/Abi/`：SyscallABI / TaskSnap / …） |
| `Lib/` | `crt0.o` `syscall.o` `libBlocks.a` 及可选 `libUi` / `libGraphics` / `libNet` / `libFsUtil` |
| `Link/user.ld` | x86_64 用户链接脚本（入口 `0x40000000`） |
| `BlocksSdk.mk` | 共用编译规则（`-std=c99 -pedantic -nostdlib`） |
| `Examples/` | Hello / Gui / Net |

本包当前为 **x86_64** 预编译目标。勿加 `-pie`、勿链宿主 glibc。

## 维护者

在 Kernel 树：

```bash
./Tools/build-sdk.sh          # → Build/BlocksSDK/ + Build/BlocksSDK.tar.gz
```
