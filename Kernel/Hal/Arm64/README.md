# Kernel / Hal / Arm64 —— ARM 64 位这条线怎么进内核

总览（建议先读）：[../../README.md](../../README.md)

本页只讲 **Arm64**（例如 QEMU 的 `virt` 机器，以及以后 ARM 开发板）。

---

## 1. 用白话说：这里在干什么？

和 PC 不同：在 QEMU 里跑 Arm64 时，我们常常让虚拟机**直接把整颗内核文件 `Kernel.elf` 塞进内存**，然后从里面的入口开始跑。

因此 Arm64 **没有**单独的 `.efi`：入口就在 **`Kernel/Hal/Arm64`**（`KernelEntry` + `KernelHandoff`），
**和内核其它部分链接在同一颗 `Kernel.elf` 里**。顶层 `Boot/` 只放 X64 的 EFI。

它负责的事情相对少：

1. 一进门先把栈准备好、把该清零的内存清掉；
2. 尽量搞清楚内存有多大（常常靠 **DTB**——设备说明书）；
3. 填好交接清单 **`BOOT_INFO`**；
4. 调用 **`KernelMain(&清单)`**，后面就和 X64 走同一套操作系统代码。

---

## 2. 什么是 DTB？（Arm 上很常见）

**DTB** = Device Tree Blob，可以想成「这块虚拟板/开发板有哪些设备、内存从哪到哪」的说明书。

- 在 QEMU 里，说明书往往由虚拟机生成，再通过约定方式交给你（有时在某个寄存器，有时在固定物理地址）。
- `KernelHandoff` 会去读它；读失败也没关系，代码里还有「默认内存大小」的退路。

真机上 DTB 文件通常跟板子厂商有关，地址也可能不同——那是以后板级配置的事；你在 QEMU 上可以先不操心。

---

## 3. 目录里都有什么？

| 文件 / 目录 | 给初学者的解释 |
| ----------- | -------------- |
| `KernelEntry.S` | 汇编入口：设栈、清 BSS → `KernelHandoff` |
| `KernelHandoff.c` | 填 `BOOT_INFO`，调 `KernelMain` |
| `Hal/` | 本架构私有 HAL 细节（如 `HalPort.h`） |
| `Board/virt/` | QEMU virt 板地址约定 |
| 头文件 | `Include/Core/BootInfo.h`（全仓一份） |
| 编译 | `cd Kernel && ./build.sh arm64` |

---

## 4. 关键名字（别被英文吓到）

| 名字 | 它是什么 |
| ---- | -------- |
| **`KernelEntry`**（在 `KernelEntry.S`） | 整颗 ELF 规定的「从这里开始跑」；接着调 `KernelHandoff` |
| **`KernelHandoff`** | Boot 阶段的 C 逻辑：组清单 |
| **`KernelMain`** | 操作系统主体大门；参数是指向 `BOOT_INFO` 的指针 |
| **`__kernel_end`** | 链接时生成的符号，表示内核镜像在内存里占到哪儿 |

---

## 5. 从加电到内核：完整故事（QEMU）

```text
QEMU 启动，并把 Kernel.elf 放进内存
  → CPU 从 KernelEntry.S 的 KernelEntry 开始执行
       · 尽量记住 DTB 在哪
       · 设置栈（C 函数需要栈才能跑）
       · 把 BSS（该清零的全局数据区）清成 0
       · 调用 KernelHandoff
  → KernelHandoff
       · 尝试从 DTB 读出内存范围；失败就用默认值
       · 填好 BOOT_INFO（哪些内存能用、内核占哪段等）
       · 调用 KernelMain(&Info)
  → 之后是三种架构共用的操作系统代码
```

如果编译时打开 bringup 模式（`BRINGUP=1`），`KernelHandoff` 可能故意不进 `KernelMain`，只做最小验证——那是开发用开关，日常默认不用管。

---

## 6. 交给下一棒的是什么？

| 交什么 | 怎么交 | 谁接 |
| ------ | ------ | ---- |
| **`BOOT_INFO`** | `KernelMain(&Info)` | Kernel 里的 Common |
| 两个 `.o` | 链接进 `Kernel.elf` | 和 HAL、Common 等一起成为一颗可启动镜像 |

编 Boot 单独两个 `.o` 时，`KernelMain`、`__kernel_end` 可能还是「未定义符号」——正常，等整颗内核链接时会补上。

---

## 7. 怎么编译？

```bash
cd ~/Blocks/Kernel && ./build.sh arm64
./build.sh
```

需要 `Blocks/Tools/Extract/（见 Tools/README.md）

---

## 8. 和 X64 / RiscV 比，我要记住什么？

- 比 X64 **少**：不用自己读盘加载内核，不用 ExitBootServices，不用先交 `BOOT_CONFIG`。
- 和 RiscV **很像**：都是「入口汇编 + KernelHandoff 组 `BOOT_INFO`」。
- RiscV **多**了多核（好几个 CPU）开机时的排队问题；Arm64 虚拟机入门可以先当单核理解。

**统一点**：带着填好的 `BOOT_INFO` 进入 `KernelMain`。
