# Boot / RiscV —— RISC-V 这条线怎么进内核

总览（建议先读）：[../README.md](../README.md)

本页只讲 **RiscV**（例如 QEMU 的 `virt` 机器，以及以后的 RISC-V 开发板）。

---

## 1. 用白话说：这里在干什么？

和 Arm64 很像：Boot **不是**单独的 EFI 小程序，而是链进 **`Kernel.elf`** 里的入口代码。

多出来的难点主要是：**RISC-V 机器上常常一开机就有好几个 CPU 核（hart）一起醒**。

所以 Boot 还要约定：

- 哪个核当「老板」（BSP）去填写 `BOOT_INFO`、调用 `KernelMain`；
- 其它核先在门口排队，等老板安排，再进内核的多核入口（`HalApMain`）。

在 QEMU 里，老板前面往往还有一位更底层的门卫：**OpenSBI**。  
你可以把它想成「比操作系统还早的小固件」：它会把「你是几号核」「设备说明书 DTB 在哪」告诉你，然后跳进我们的 ELF。

---

## 2. OpenSBI 是必须自己写的吗？

**在 QEMU 练习时：不用。** QEMU 会带上 OpenSBI，你的内核当它的「下一棒」。

真机上很多板子也是：厂商已经提供 OpenSBI（再加 U-Boot）。  
我们通常是 **按约定加载**，而不是从零实现一整套固件。那是以后板级/Runtime 的工作。

---

## 3. 目录里都有什么？

| 文件 | 给初学者的解释 |
| ---- | -------------- |
| `Boot.S` | 汇编入口：抢 BSP、设栈、清 BSS；其它核去排队 |
| `Boot.c` | **`BootMain`**：只有老板核会跑到这里，组 `BOOT_INFO` |
| `BootInfo.h` / `BootTypes.h` | 交接清单（与 Arm64 那份应对齐） |
| `build.sh` | 交叉编译 → **`Build/*.o`** |

---

## 4. 关键名字

| 名字 | 它是什么 |
| ---- | -------- |
| 汇编入口（`Boot.S`） | OpenSBI / 加载器跳进来的地方；会带上 hart 号和 DTB 指针 |
| **`BootMain`** | 老板核上的 C：填写 `BOOT_INFO`，调用 `KernelMain` |
| **`KernelMain`** | 三种架构汇合后的操作系统大门 |
| **`HalApMain`** | 其它 CPU 核被放行后进入的内核函数（不做完整 Boot 清单） |
| `gBspClaimed` 等 | 用来「抢老板」和给其它核发信号的全局变量 |

---

## 5. 从加电到内核：完整故事（QEMU）

```text
QEMU 启动 → OpenSBI 先跑
  → 跳进我们的 Boot.S 入口
       每个核都可能跑到这里
       · 用原子操作抢「我是不是第一个」（BSP）
       · 【老板核】设栈、清 BSS → 调用 BootMain
       · 【其它核】在 SecondaryPark 里睡觉，等信号
  → BootMain（只有老板）
       · 看 DTB，弄清内存；不行就用默认值
       · 填 BOOT_INFO（含 DTB 地址）
       · 调用 KernelMain(&Info)
  → Common（和 X64、Arm64 同一套主体逻辑）

其它核稍后：
  → 被唤醒 → HalApMain → 加入多核干活
```

---

## 6. 交给下一棒的是什么？

| 谁 | 交什么 | 怎么交 |
| -- | ------ | ------ |
| 老板核 | **`BOOT_INFO`** | `KernelMain(&Info)` |
| 其它核 | 不交完整 Boot 清单 | 汇编里转到 `HalApMain` |
| 编译产物 | `Boot_asm.o`、`Boot.o` | 链接进 `Kernel.elf` |

---

## 7. 怎么编译？

```bash
cd ~/OpenBox/Boot/RiscV
./build.sh
```

需要 RISC-V 交叉编译器（见仓库 `Tools/README.md`）。

---

## 8. 和 X64 / Arm64 比，我要记住什么？

| | X64 | Arm64 | **RiscV** |
|--|-----|-------|-----------|
| 典型产物 | `.efi` | `.o` 链进内核 | `.o` 链进内核 |
| 要不要自己读内核文件 | 要 | 不要 | 不要 |
| 第一次交的清单 | `BOOT_CONFIG` | `BOOT_INFO` | `BOOT_INFO` |
| 多核开机麻烦吗 | 另有一套（偏后面） | 入门可先当单核 | **Boot 入口就要处理** |

**统一点仍然是**：老板核带着 `BOOT_INFO` 进入 `KernelMain` 之后，三种架构读同一套操作系统代码。
