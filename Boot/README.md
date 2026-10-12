# Boot —— UEFI 独立引导（X64）

总览：[../README.md](../README.md)

```text
Boot/
  BootPkg/     # UEFI 应用包（DSC/INF/源码）
  EDK2/        # 裁剪工具包（含 Conf/）
  build.sh     # WORKSPACE=Boot，PACKAGES_PATH=Boot:EDK2，CONF_PATH=EDK2/Conf
  Build/       # → BOOTX64.EFI
```

Arm64 / RiscV 入口在 **`Kernel/Hal/<Arch>/`**，不在这里。

```bash
cd ~/Blocks/Boot && ./build.sh
./build.sh DEBUG=1          # 更多 Boot 调试打印
```

---

## 1. 用白话说：这里在干什么？

在 PC 上，机器一开电，先跑的是主板里的 **UEFI 固件**（可以想成出厂自带的「前台软件」）。

我们的 Boot **不是**内核本身，而是一个前台能听懂的小程序：

1. 固件找到并运行 **`BOOTX64.EFI`**；
2. 这个程序布置串口、屏幕，从磁盘读出 **`Kernel.elf`**；
3. 向固件告别（`ExitBootServices`）；
4. 跳进内核，并递上一包材料 **`UEFI_BOOT_CONFIG`**。

注意：X64 **不在 Boot 里**直接填好最终的 `BOOT_INFO`。  
那包 `UEFI_BOOT_CONFIG` 会先到内核 HAL 的 **`KernelHandoff`**，转成 `BOOT_INFO`，再进三种架构共用的 `KernelMain`。

所以你会觉得 X64 的 Boot **文件特别多**——因为 PC 这条路本来就要自己当「加载器」。

---

## 2. 目录里都有什么？

| 文件 | 给初学者的解释 |
| ---- | -------------- |
| `BootPkg/Boot.c` | 节目单：只按顺序调下面六步 |
| `Serial.c` / `.h` | ① 串口 COM1 |
| `Video.c`（+ Score/Edid/Theme） | ② GOP 选模 / 设分辨率 |
| `LoadKernel.c` | ③ 读盘装入 `Kernel.elf` |
| `FillRsdp.c` | ④ ACPI RSDP |
| `FillXhci.c` | ⑤ PCI 上找 xHCI |
| `JumpToKernel.c` | ⑥ ExitBootServices + 跳内核 |
| `UefiBootConfig.h` | 交接清单 `UEFI_BOOT_CONFIG` |
| `BootPrivate.h` | 本包内部声明 |
| `EDK2/` | 裁剪工具包 |
| `build.sh` | → `Build/BOOTX64.EFI` |

---

## 3. `UefiMain` 在干什么？（节目单）

读 `BootPkg/Boot.c` 时，按这个顺序理解即可：

1. **Serial** → `BootSerialInitialize` / `BootSerialBanner`
2. **Video** → `GetVideoInfo` / `SetVideoMode`
3. **LoadKernel** → `BootLoadKernel`
4. **FillRsdp** → `BootFillRsdp`（并记下 `SystemTable`）
5. **FillXhci** → `BootFillXhci`
6. **JumpToKernel** → `JumpToKernel`

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
  → 调用内核入口（函数指针），参数是 &UEFI_BOOT_CONFIG
  → KernelHandoff(UEFI_BOOT_CONFIG*)
       · 转成 BOOT_INFO
       · 再调用 KernelMain
  → 之后是三种架构共用的操作系统代码
```

---

## 5. 交给下一棒的是什么？

| 交什么 | 怎么交 | 谁接 |
| ------ | ------ | ---- |
| 结构体 **`UEFI_BOOT_CONFIG`** | 作为函数参数传给 **`KernelHandoff`** | 内核 HAL（X64） |
| 内核代码本身 | 已经按 ELF 装进内存；**`EntryAddress`** 是入口地址数字 | CPU 从该地址开始执行 |

**`KernelHandoff`**：三架构角色名。X64 上由 HAL 实现，入参 `UEFI_BOOT_CONFIG*`；Arm/RiscV 在 `Kernel/Hal/<Arch>/`，入参为 DTB 等。都是填 `BOOT_INFO` 再进 `KernelMain`。

---

## 6. 编译产物

成功时：`Build/BOOTX64.EFI`（以及 `BootPkg.efi` 副本）。中间文件在 `Build/BootPkg/`，一般不用提交。

---

## 7. 和 Arm / RiscV 比，我为什么要写这么多？

| | X64 | Arm64 / RiscV（QEMU 常见玩法） |
|--|-----|--------------------------------|
| 谁加载内核文件 | **Boot 自己** | 加载器已经放好了整颗 ELF |
| 要不要和 UEFI 打交道 | **要** | 通常没有 |
| Boot 编出什么 | 独立的 `.efi` | 几个 `.o`，链进内核 |
| 第一次交出的清单 | `UEFI_BOOT_CONFIG` | 直接是 `BOOT_INFO` |

**汇合点不变**：大家都经 **`KernelHandoff`** 填好 `BOOT_INFO` 再进 `KernelMain`。X64 多的是前面的 EFI + `UEFI_BOOT_CONFIG`。
