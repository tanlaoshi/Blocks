# Boot / X64 —— 普通 PC（x86-64）怎么把系统扶起来

总览（建议先读）：[../README.md](../README.md)

本页只讲 **X64**：也就是常见 Windows/Linux PC、以及 QEMU 模拟的 x86_64 机器。

---

## 1. 用白话说：这里在干什么？

在 PC 上，机器一开电，先跑的是主板里的 **UEFI 固件**（可以想成出厂自带的「前台软件」）。

我们的 Boot **不是**内核本身，而是一个前台能听懂的小程序：

1. 固件找到并运行 **`BOOTX64.EFI`**；
2. 这个程序布置串口、屏幕，从磁盘读出 **`Kernel.elf`**；
3. 向固件告别（`ExitBootServices`）；
4. 跳进内核，并递上一包材料 **`X64_BOOT_CONFIG`**。

注意：X64 **不在 Boot 里**直接填好最终的 `BOOT_INFO`。  
那包 `X64_BOOT_CONFIG` 会先到内核的 HAL，**翻译**成 `BOOT_INFO`，再进三种架构共用的 `KernelMain`。

所以你会觉得 X64 的 Boot **文件特别多**——因为 PC 这条路本来就要自己当「加载器」。

---

## 2. 目录里都有什么？

| 文件 / 目录 | 给初学者的解释 |
| ----------- | -------------- |
| `Boot.c` | 只有一个入口函数 **`UefiMain`**，按顺序调用别人，像「节目单」 |
| `BootSerial.c` / `.h` | 串口（COM1）打印，方便在没图形界面时看日志 |
| `Video/` | 和显卡/分辨率打交道（GOP） |
| `BootKernel.c` / `BootKernelLoad.c` | 从磁盘找到 `Kernel.elf`，检查并加载到内存 |
| `BootAcpi.c` | 找 ACPI 的 RSDP（电源/硬件信息的入口地址） |
| `BootPci.c` | 在 PCI 总线上找 USB 控制器（xHCI）地址 |
| `BootJump.c` | 拿到最终内存图、退出 Boot 服务、跳进内核 |
| `BootHandoff.h` | **交接清单** `X64_BOOT_CONFIG` 的字段定义（Boot 和内核都要认） |
| `BootPrivate.h` | 本目录各 `.c` 互相调用时用的声明 |
| `EDK2/` | 用来编译 EFI 的工具包（很大，一般不用手改） |
| `build.sh` | 一键编译；成功后产物在 **`Build/BOOTX64.EFI`** |

---

## 3. `UefiMain` 在干什么？（节目单）

读 `Boot.c` 时，按这个顺序理解即可：

1. **开串口、打欢迎横幅** → `BootSerialInitialize` / `BootSerialBanner`
2. **搞定显示** → `GetAndSetVideo`（在 `Video/` 里）
3. **加载内核文件** → `BootLoadKernel`（读盘 + 解析 ELF）
4. **填 RSDP** → `BootFillRsdp`
5. **记下 SystemTable**（固件留给运行时用的一张大表指针）
6. **填 USB 控制器地址** → `BootFillXhci`
7. **退出固件服务并跳转** → `JumpToKernel`

某一步失败，通常会在串口打一行 `Boot: … Failed`，然后返回错误。

---

## 4. 从加电到内核：完整故事

```text
电脑加电
  → UEFI 固件启动
  → 找到并运行 BOOTX64.EFI
  → 进入我们的 UefiMain
       · 串口可以说「我活着」
       · 屏幕模式选好
       · 从盘上读出 Kernel.elf，放到约定内存
       · 记下 ACPI / USB 等信息
  → ExitBootServices（之后不能再随便问固件要服务了）
  → 调用内核入口（函数指针），参数是 &X64_BOOT_CONFIG
  → 内核 HAL 收到 X64_BOOT_CONFIG
       · 翻译成 BOOT_INFO
       · 再调用 KernelMain
  → 之后是三种架构共用的操作系统代码
```

---

## 5. 交给下一棒的是什么？

| 交什么 | 怎么交 | 谁接 |
| ------ | ------ | ---- |
| 结构体 **`X64_BOOT_CONFIG`** | 作为函数参数传给内核入口（指针） | 内核 HAL（现网在 ToyKernel 的 `Startup.c`） |
| 内核代码本身 | 已经按 ELF 装进内存；结构体里有一个**入口地址数字** | CPU 从该地址开始执行 |

结构体里的 **`EntryAddress`** 是内核镜像入口的 **64 位地址数字**，不要和内核里的入口函数名混成一个东西。

---

## 6. 怎么编译？

```bash
cd ~/OpenBox/Boot/X64
./build.sh                 # 普通编译
./build.sh DEBUG=1         # 打开更多 Boot 调试打印
```

成功时：`Build/BOOTX64.EFI`（以及可能的 `ToyBoot.efi` 副本）。

中间文件还会出现在 `EDK2/Build/` 下，那是工具链的临时产物，一般不用提交到 git。

---

## 7. 和 Arm / RiscV 比，我为什么要写这么多？

| | X64 | Arm64 / RiscV（QEMU 常见玩法） |
|--|-----|--------------------------------|
| 谁加载内核文件 | **Boot 自己** | 加载器已经放好了整颗 ELF |
| 要不要和 UEFI 打交道 | **要** | 通常没有 |
| Boot 编出什么 | 独立的 `.efi` | 几个 `.o`，链进内核 |
| 第一次交出的清单 | `X64_BOOT_CONFIG` | 直接是 `BOOT_INFO` |

**汇合点不变**：大家都要在进入 `KernelMain` 时拥有 `BOOT_INFO`。X64 只是多了「翻译」那一站。
