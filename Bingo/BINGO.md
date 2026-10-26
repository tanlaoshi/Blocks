# BINGO — 五分钟进 `blocks>`

> 适用：App / 驱动 / 贡献 / 学生。研究源码可先读 [`System/`](System/README.md)，再回来跑通一次。

## 1. 环境

```bash
cd ~/Blocks
source ./env.sh          # 短名：build / run / boot / smoke …
```

工具链与换机：[`Archive/同步.md`](../Archive/同步.md)。

## 2. 编与跑（x64 课机 / QEMU）

```bash
build                    # Kernel（默认同步 Runtime rootfs）
boot                     # 可选：BOOTX64.EFI
run                      # QEMU；看串口
```

Guest 提示符：`blocks>`。

## 3. 编出第一颗自己的 ELF

```bash
cd ~/Blocks/Kernel && ./Tools/build-sdk.sh          # 一次：Build/BlocksSDK
cp -a ~/Blocks/Bingo/App/Template ~/MyApp
make -C ~/MyApp                                     # → build/MYAPP.ELF
```

树内现成样例：`Bingo/App/Examples/{Hello,Gui,Net}`（见 [`App/Guide.md`](App/Guide.md)）。  
Guest：`exec HELLO.ELF`（或商店 `store combo hello`）。

## 4. 红字

| 现象 | 先看 |
| ---- | ---- |
| `printf`「没输出」 | QEMU **串口**（焦点在用户窗时不进客户区） |
| `socket` 失败 | Guest 先 `lwip on`；Arm virt 常无 lwIP |
| 未知 syscall | 返回 **-38**；号表 `Kernel/Include/Abi/SyscallABI.h` |

下一步按身份回 [`README.md`](README.md)。
