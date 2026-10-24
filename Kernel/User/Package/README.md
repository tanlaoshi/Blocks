# 用户程序模板（PR-L1）

用 Blocks CRT（`<stdio.h>` / `libBlocks`）编一个可 `exec` 的 ELF。

- 教程：[`Documents/手册/应用开发指南.md`](../../../Documents/手册/应用开发指南.md)
- **离树 SDK**（推荐课外）：`cd Kernel && ./Tools/build-sdk.sh` → `Build/BlocksSDK.tar.gz`

## 快速开始（树内）

```bash
cd /path/to/Kernel && ./build.sh   # 先有 CRT / libBlocks

cd User/Package
make                               # → build/MYAPP.ELF
cp build/MYAPP.ELF ../../../Runtime/RootFs/X64/MYAPP.ELF
# Guest：exec MYAPP.ELF
# 注意：卷根没有时会找 Class/MYAPP.ELF；勿留旧 Ui 陈货（会饿死鼠标）。
```


换名：`make PROG=HELLO2 SRCS=main.c`。

课外独立目录：复制本目录，构建时指定树根：

```bash
make BLOCKS_KERNEL=/path/to/Kernel
```

## 头文件

| Include | 用途 |
|---------|------|
| `<stdio.h>` / `<stdlib.h>` / `<string.h>` / `<unistd.h>` | libc 子集 |
| `<fcntl.h>` / `<errno.h>` / `<signal.h>` | 文件与信号 |
| `<Blocks/syscall.h>` | 系统调用号（`SYS_*`）+ `blocks_syscall` |
| `<Blocks/version.h>` | `CRT_VERSION_*` |
| `<Blocks.h>` | 伞头（demo 用） |

`-I` 由 `BlocksUser.mk` 自动加。

## 链接

`crt0.o` + `syscall.o` + `libBlocks.a` + `user.ld`。

GUI / 网络模板：

```make
# User/Package/Gui/
EXTRA_LIBS = $(BLOCKS_KERNEL)/User/Library/Ui/libUi.a \
             $(BLOCKS_KERNEL)/User/Library/Graphics/libGraphics.a

# User/Package/Net/
EXTRA_LIBS = $(BLOCKS_KERNEL)/User/Library/ToyNet/libToyNet.a
```

## 不要做的事

- 不要 `#include` 内核私有头或调用 `Hal*`
- 不要依赖宿主 glibc（`-nostdlib`）；不要 `-pie`
