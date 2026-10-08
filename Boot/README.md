# Boot —— 开机时「把电脑交给操作系统」的那一段

这份说明写给：**学过一点 C、第一次看本仓库** 的同学。  
不要求你先懂 UEFI、OpenSBI、链接脚本。遇到专有名词，文里会先用白话解释。

想先看某一架构怎么编、有哪些文件，直接进子目录：

| 你想了解 | 打开 |
| -------- | ---- |
| 普通 PC（x86-64）怎么引导 | [`X64/README.md`](X64/README.md) |
| ARM 虚拟机 / 开发板这条线 | [`Arm64/README.md`](Arm64/README.md) |
| RISC-V 这条线 | [`RiscV/README.md`](RiscV/README.md) |

---

## 1. 用生活例子理解 Boot

电脑上电之后，**还不能直接跑我们写的操作系统**。中间有一段「接待工作」：

1. 让机器能打字（串口）或能亮屏（显卡）；
2. 知道内存哪儿能用、哪儿不能乱动；
3. 找到「真正的操作系统」（Kernel）在哪儿；
4. 把一份**清单**交给操作系统，然后说：你接手吧。

在本项目里，负责这段接待工作的代码，都放在 **`Boot/`** 目录下。  
三种 CPU 架构（X64、Arm64、RiscV）**接待方式不一样**，所以分成三个文件夹；但接待结束之后，操作系统主体走**同一条路**。

可以把三种架构想成三种「进大楼的门」：

- **X64**：像正规写字楼——先过前台（UEFI 固件），前台程序（我们的 EFI）再开门、递文件。
- **Arm64 / RiscV（在 QEMU 虚拟机里）**：像实验楼侧门——门卫（QEMU / OpenSBI）已经把整栋楼图纸塞进你手里，你进门简单收拾一下就能开会。

---

## 2. 几个词先搞懂（后面表格会用到）

| 词 | 白话 |
| -- | ---- |
| **架构** | CPU 的「方言」。X64、Arm64、RiscV 指令不一样，开机套路也不一样。 |
| **固件** | 出厂就在板子/虚拟机里的小程序，比操作系统更早跑（PC 上常叫 UEFI/BIOS）。 |
| **Kernel（内核）** | 操作系统的核心程序。Boot 的目标就是把它扶上马。 |
| **ELF** | 一种可执行文件格式。Linux、本项目的内核常用这种。 |
| **EFI / `.efi` 文件** | PC 上 UEFI 能直接运行的小程序。X64 的 Boot 编出来就是 `BOOTX64.EFI`。 |
| **`.o` 文件** | 编译器把 `.c` / `.S` 编成的「半成品」，还要再和别的文件**链接**成完整程序。 |
| **链接** | 把多个 `.o` 拼成一颗完整可执行文件（例如 `Kernel.elf`）。 |
| **DTB** | 「这块板子长什么样」的说明书（设备树），多用于 Arm / RiscV。 |
| **`BOOT_INFO`** | 我们自己定的一张**交接清单**（内存、屏幕、内核占哪段……）。三种架构最终都要让内核拿到这张清单。 |
| **`X64_BOOT_CONFIG`** | **仅 X64 Boot** 用的清单（Arm/RiscV 没有），更贴近 UEFI 原始数据。字段 **`EntryAddress`** = 内核入口地址。 |
| **`HalGetBootConfig`** | **仅 X64 HAL**：接收 `X64_BOOT_CONFIG*`，转成 `BOOT_INFO`，再进 `KernelMain`。 |
| **HAL** | Kernel 里「和硬件打交道」的那一层，不同架构可以不一样。 |
| **Common** | Kernel 里三种架构**共用**的那一大段逻辑（调度、文件、界面等）。 |
| **`KernelMain`** | Common 的大门。进这扇门时，约定：**手里已经有 `BOOT_INFO`**。 |

---

## 3. 最重要的一句话

**在进 `KernelMain` 之前，三种架构可以各走各的路；  
从「已经拿着 `BOOT_INFO` 调用 `KernelMain`」开始，三种架构走同一条操作系统代码。**

所以你会看到：X64 的 Boot **文件更多、步骤更长**——不是写乱了，而是 PC 开机本来就要多做几件事。

---

## 4. 三架构从开机到统一：逐步对照表

下面每一行是一个「时间点」。  
「要做」= 这一步有实质工作；「不做 / 很轻」= 这一步可以跳过或几乎没有。

| 时间顺序 | 这一步在干什么（白话） | **X64（PC）** | **Arm64** | **RiscV** |
| -------- | ---------------------- | ------------- | --------- | --------- |
| ① | 谁先跑起来？ | 电脑固件（UEFI）先跑，再加载我们的 **`BOOTX64.EFI`** | 虚拟机/加载器用 **`-kernel`** 等方式，把整颗 **`Kernel.elf`** 放进内存 | 先有 **OpenSBI**（一种很底层的固件），再跳进我们的 **`Kernel.elf`** |
| ② | 我们第一段自己的代码叫什么？ | C 函数 **`UefiMain`**（固件规定的入口） | 汇编 **`Boot.S`** 里的入口（先设栈等） | 同 Arm；还要处理「多个 CPU 核谁当老大」 |
| ③ | 要不要自己初始化串口、显卡？ | **要**（Serial、Video、PCI 等，所以代码多） | 开机早期通常**很轻**或不做完整一套 | 同 Arm |
| ④ | 谁把「内核文件」读进内存？ | **Boot 自己读**（从磁盘/ESP 找 `Kernel.elf`） | **不用读第二次**——加载器已经把整颗 ELF 放好了 | 同 Arm |
| ⑤ | 要不要向固件「正式告别」？ | **要**（`ExitBootServices`：之后不能再随便用 UEFI 服务） | 没有这套 UEFI 手续 | 没有 |
| ⑥ | Boot 递出去的第一份材料是什么？ | **`X64_BOOT_CONFIG`**（UEFI 风格的原始材料包） | 直接组好 **`BOOT_INFO`** | 直接组好 **`BOOT_INFO`** |
| ⑦ | 下一棒是谁？ | 内核 HAL：**`HalGetBootConfig(X64_BOOT_CONFIG*)`** → 转成 `BOOT_INFO` → `KernelMain` | 直接进入 **`KernelMain(清单)`** | 同 Arm（其它 CPU 核走另一条小门，不组清单） |
| ⑧ | **三种架构在这里汇合** | ↓ | ↓ | ↓ |
| **统一点** | 操作系统主体开始 | **调用 `KernelMain`，并且已经有一份 `BOOT_INFO`** | 相同 | 相同 |

用箭头画一次：

```text
【X64】
  固件 → BOOTX64.EFI（Boot 做很多事）
       → 交出 X64_BOOT_CONFIG（含 EntryAddress）
       → HalGetBootConfig(…) 转成 BOOT_INFO
       → KernelMain(…)     ←── 统一从这里开始
       → 三种架构共用的 Common

【Arm64 / RiscV】
  加载器/OpenSBI → 内存里已有 Kernel.elf
       → Boot 里一小段（汇编 + BootMain）组好 BOOT_INFO
       → KernelMain(…)     ←── 和 X64 汇合
       → 同样的 Common
```

---

## 5. 交接时「清单」里大概有什么（人话）

不必背字段名，先建立感觉：

**大家最终都要的 `BOOT_INFO`（给 `KernelMain`）：**

- 屏幕缓冲区在哪儿、多大（有的机器暂时没有，可以填 0）；
- 内存哪些段可以拿来当「空地」分配，哪些要保留；
- 内核自己占用了哪一段地址；
- （Arm/RiscV）设备树 DTB 在内存的哪里（没有就填 0）。

**只有 X64 Boot 先交出的 `X64_BOOT_CONFIG`：**

- 上面很多信息的「原材料」形式（例如 UEFI 的内存图还没整理成我们的段表）；
- 以及 ACPI、SystemTable、USB 控制器地址等 **PC/UEFI 才有的东西**。  
  这些先交给 HAL 的 **`HalGetBootConfig`**，转成标准的 `BOOT_INFO` 后再进 `KernelMain`。

头文件在哪里（给以后改代码的人）：

- X64：[`X64/BootHandoff.h`](X64/BootHandoff.h)
- Arm64：[`Arm64/BootInfo.h`](Arm64/BootInfo.h)
- RiscV：[`RiscV/BootInfo.h`](RiscV/BootInfo.h)  
  （Arm 与 RiscV 两份 `BootInfo.h` 内容应对齐，改一张记得改另一张。）

---

## 6. 编出来的东西长什么样？为什么目录不共用？

三种架构**编完的文件格式不一样**，所以各自有自己的 `Build/` 文件夹，不会混在一起。

| 架构 | 怎么编 | 编出什么 | 下一环怎么用 |
| ---- | ------ | -------- | ------------ |
| X64 | 进入 `Boot/X64`，运行 `./build.sh` | **`Build/BOOTX64.EFI`** —— 固件能直接跑的小程序 | 放进启动盘 / 虚拟机固件能找到的位置；它再去加载内核 |
| Arm64 | 进入 `Boot/Arm64`，运行 `./build.sh` | **`Build/Boot_asm.o`** 和 **`Boot.o`** —— 半成品 | 和内核其它 `.o` **链接**成一颗 `Kernel.elf` |
| RiscV | 进入 `Boot/RiscV`，运行 `./build.sh` | 同上，两个 `.o` | 同上 |

目录长这样：

```text
Boot/
  X64/     …源码…   EDK2/     Build/BOOTX64.EFI
  Arm64/   …源码…            Build/*.o
  RiscV/   …源码…            Build/*.o
```

X64 用本目录 `EDK2`。Arm64/RiscV 交叉编译器与 Kernel **共用**，在仓库顶层 [`Tools/Extract/`](../Tools/README.md)。

---

## 7. 名字容易混？（先有个印象即可）

名字以后可能还会统一整理；现在读代码时只要分清「它是地址还是函数」：

| 你看到的名字 | 通常是什么意思 |
| ------------ | -------------- |
| Arm/RiscV 汇编里的入口 | 机器一跳进来的地方（在 `Boot.S`） |
| X64 的 `UefiMain` | EFI 程序的 `main`（固件调用它） |
| Arm/RiscV 的 `BootMain` | Boot 里用 C 填写 `BOOT_INFO` 的函数 |
| `KernelMain` | 三种架构汇合后的操作系统大门 |
| `X64_BOOT_CONFIG.EntryAddress` | 内核从哪儿开始执行的**数字地址** |
| **`HalGetBootConfig`**（仅 X64 HAL） | 接收 `X64_BOOT_CONFIG*`，转成 `BOOT_INFO`，再调 `KernelMain` |

更细的「哪个文件、哪个函数、一步步怎么跑」写在各架构自己的 README 里。

---

## 8. 读完可以带走的三句话

1. **Boot = 开机接待员**；三种架构接待流程可以不同。  
2. **X64 接待最忙**（自己加载内核、和 UEFI 打交道），所以代码最多。  
3. **汇合点是：带着 `BOOT_INFO` 进入 `KernelMain`**——从这里起，大家读同一套操作系统逻辑。
