# Raspberry Pi 3B+ 真机启动排障历程（rpi-3）

> **日期**：2026-10-24 起（回家 SD 手测）  
> **硬件**：Pi 3B+ · 专用 SD（原 Raspberry Pi OS 改 Blocks）· USB-TTL 3.3V（GPIO 14/15/GND）· HDMI 可看 U-Boot  
> **目标（rpi-3）**：U-Boot → Blocks 串口见横幅（`Boot: Arm64` / `board: Raspberry/Pi3BPlus`）  
> **代码主战场**：`HAL/Arm64/Board/Raspberry/Pi3BPlus/`、`BringUp/Arm64/`、`HAL/Arm64/{HalSerial,PageTable,Exception,Smp}.c`  
> **摆盘**：`prepare-pi-sd` → `Build/Pi3BPlus-sd/`；说明以 **`README-SD.txt`** 为准（勿信过期 `README-SD.md` 旧稿）  
> **状态**：🔧 进行中。**CoolTerm 已见 `Boot: Mod Serial/PhysicalMemory/VirtualMemory`**（内核已跑）；HDMI 停在 U-Boot「Starting」**正常**（本柱无 FB）。  
> **姊妹**：板包 [`NOTES.md`](../../../Kernel/HAL/Arm64/Board/Raspberry/Pi3BPlus/NOTES.md)；日档 [路线图1023 · rpi-desk](../../路线图1023.md#rpi-desk)（手测 ⏸，活 ★ 在 1024）

---

## 0. 一句话（当前结论）

1. **U-Boot 链已钉死**：stock **2021.01** + `enable_uart=1` + `core_freq=250` + **无** `dtoverlay=disable-bt` → 串口/HDMI 均有 `U-Boot>`。  
2. **内核链接必须在 DRAM 内**：virt 的 `0x40000000` 超出 3B+ ~948MiB → `bootelf` 写飞复位。板级 `link.ld` @ **`0x80000`**。  
3. **控制台在 mini-UART**（证据：加 `disable-bt` 则整卡无 U-Boot 提示）。Blocks 已 `BOARD_UART_MINI=1` @ `0x3F215000`。  
4. **`bootelf` 可以，但 ELF 暂存必须在 `0x20000000`**：load 到 `0x80000` 再 bootelf → 自覆盖 → `Starting at 0x0`。  
5. **串口已通、内核已进模块表**（CoolTerm）。HDMI **不会**离开 U-Boot 最后一行——`BOARD_HAS_FRAMEBUFFER=0`，无 Present。以 CoolTerm 为准。  
6. **卡点演进**：曾停 VirtualMemory（开 MMU）→ 已 skip MMU；再刷后整段只有 `Starting`+`*`（无 `+`）→ **EL2→EL1 eret 仍挂**。已撤降级，组合：**EL2 停留 + skip MMU**（待再刷一次）。

---

## 1. 少换卡原则（给 Agent / 自己）

| 动作 | 要不要拔卡 | 说明 |
|------|------------|------|
| 只改 U-Boot 命令 / `printenv` / `md.b` / cache flush | **否** | 卡在板子上即可 |
| 只改 `config.txt` / 换 `Kernel.bin` | **要** | 才刷 FAT |
| 改代码但逻辑未过「纸面/反汇编」门 | **先不刷** | 先 `build` + `objdump`/`readelf` 自证 |
| 同一次会话连刷多次「碰运气」 | **禁止** | 无新假设不刷卡 |

**刷卡前自问**：这次卡上字节相对上次手测，**到底哪一个文件变了**？没变就别拔。

---

## 2. 当前正确手测配方（勿改口令）

### 2.1 `config.txt`（`[all]` 段关键）

```text
kernel=u-boot.bin
enable_uart=1
arm_64bit=1
auto_initramfs=0
core_freq=250
# 禁止 dtoverlay=disable-bt（会弄哑本版 U-Boot）
```

U-Boot 二进制：已验证的 **stock 2021.01** 作 `kernel8.img` / `u-boot.bin`（勿混 DEBUG_UART / 2024/2026 未验证组合）。

### 2.2 U-Boot 提示符

**常态**：`u-boot.bin` = **stock 2021.01** → `distro_bootcmd` 自动 `source /boot.scr`，**上电不用敲**。  
（若卡上误放了 `bootflow scan` 新包，会停提示符——换回 stock。）

**救砖 / 自动失败**：串口输入常丢字 → **HDMI 键盘**敲两行，CoolTerm **只看输出**：

```text
load mmc 0:1 0x1000000 boot.scr
source 0x1000000
```

（脚本内：高址 load `Kernel.elf` @ `0x20000000` + `pi.dtb` + `bootelf -p`，自带段 flush。）

| 看到什么 | 含义 |
|----------|------|
| `Starting … 0x00000000` | ELF 叠在 0x80000 上自覆盖，或 load 失败；确认暂存 **0x20000000** 且 `ls mmc 0:1` 有 `Kernel.elf` |
| `Starting … 0x00080000` 后无字 | 已跳进内核链接址；若无 `*` 再查 UART/EL |
| `*` | 已执行 `KernelEntry` 早期 putc |
| `*` 后无 `+` | 卡在 EL2→EL1 或 BSS |
| `+` 后有 `Boot:` | Handoff 串口通 |
| `Boot: EL=1` | 已在 EL1 |
| 迅速多行后复位 | 曾见：EL2 MMU 自测 / PSCI 等（见 §3 已修项） |
| `icache flush` unknown | **预期**；勿再试，改用上表 bootelf 高址 |

可选核对：`md.b 0x20000000 4` 应为 `7f 45 4c 46`（ELF 魔数）；bootelf 成功后再 `md.b 0x80000 4` ≈ `f3 03 00 aa`。

`pi.dtb` = 卡上 `bcm2710-rpi-3-b-plus.dtb` 短名（优先 bcm2710）。

备选（无 cache 命令时 **不推荐** 作主路径）：`Kernel.bin` + `go 0x80000`（易无 `*`）。

---

## 3. 方法编年（已试 · 勿回头）

### 3.1 固件 / U-Boot 选型

| # | 尝试 | 现象 | 结论 |
|---|------|------|------|
| U1 | Debian/`rpi_arm64` 等发行 U-Boot | 3B+ 上失败/不适配 | 不用 |
| U2 | U-Boot **2026.10** + DEBUG_UART | 串口狂刷 `<debug_uart>`，约 1–2s 复位环 | 废；像调试口未真正起来 |
| U3 | U-Boot **2024.10** | 无串口、绿灯也不闪（硬挂） | 废 |
| U4 | U-Boot **2021.01** + DEBUG 乱配（曾弄成 PL010） | 绿灯闪几下灭、无提示 | 废 |
| U5 | 2021.01 stock + **`disable-bt`** | 整卡无 U-Boot 提示 | **禁止**对本版 U-Boot 加 disable-bt |
| U6 | 2021.01 stock + `enable_uart` + `core_freq=250`、**无** disable-bt | 串口+HDMI 均有 `U-Boot>` | **✅ 采用** |

### 3.2 加载方式 / 链接地址

| # | 尝试 | 现象 | 结论 |
|---|------|------|------|
| L1 | ELF 链在 virt **`0x40000000`** + bootelf | 迅速复位回 U-Boot | 越界：DRAM≈948MiB |
| L2 | 板级 `link.ld` @ **`0x80000`** | 可 `go` 到该址 | **✅ 采用** |
| L3 | `load` ELF 到 `0x80000` 再 `bootelf -p` 同址 | `Starting at 0x0` | PHDR 拷贝盖掉 ELF 头 |
| L4 | `bootelf -p` 但 ELF 仍在 `0x80000` | `Starting at 0x0` | 自覆盖；**不是** bootelf 本身无用 |
| L5 | `Kernel.bin` + `go 0x80000` | 能 Starting@80000，但常无 `*` | `go` 不刷 I-cache；本板又无 flush 命令 |
| L6 | 跟旧 README 乱敲 bootelf | 又见 `0x0` | 文档债 |
| L7 | `load 0x20000000 Kernel.elf` + bootelf | 曾报 `no elf image at address 0x20000000` | load 未成功或地址/文件名不对；先看 load 回显字节数 / `md.b` 魔数 |
| L8 | 某次 Starting @ `0x30000` | 跳错入口 | 勿用；核对 load 地址与文件 |
| L9 | Starting @ `0x80000` + CoolTerm `Boot: Mod …` | **✅ 内核已跑** | 以串口为准；HDMI 停 Starting 可忽略 |


### 3.3 串口（PL011 vs mini-UART）

| # | 尝试 | 现象 | 结论 |
|---|------|------|------|
| S1 | Blocks 打 PL011 `0x3F201000` + 无 disable-bt | `go` 后卡住无内核字（U-Boot 仍有字） | 口不一致 |
| S2 | `dtoverlay=disable-bt` 想绑 PL011 | U-Boot 整卡无提示 | 不能为了内核毁 U-Boot |
| S3 | `BOARD_UART_MINI=1`，基址 `0x3F215000` | 曾一度有「十几行」内核日志 | **✅ 采用**（跟 U-Boot 同口） |
| S4 | 入口 asm 死等 LSR 再 putc | 可能挂死在 Starting 后无 `*` | 已改：ENABLES + **有限自旋** |

### 3.4 异常级 / MMU / virt 路径误用

| # | 尝试 | 现象 | 结论 |
|---|------|------|------|
| E1 | 在 EL2 跑，开 **SCTLR_EL1** MMU + `HalPagingSelfTest` 碰 `0x180000000` | 迅速复位 | EL2 外部中止且无合适 VBAR → 复位 |
| E2 | EL≥2 跳过开 MMU；装 `VBAR_EL2` | 防 EL2 自测复位 | 不够：EL1 仍会开 MMU |
| E2b | 真机 **一律** `vmm: skip MMU (board)` | **待一次刷卡** | 手测停在 VirtualMemory 的对策 |
| E3 | EL2→EL1 且 `SCTLR_EL1=0` | eret 挂死、无 `*` | RES1 必须 |
| E3b | EL2→EL1 且 SCTLR=`0x30d00800` | 仅 `*`、无 `+`（死在 eret 与 BSS 之间） | **暂撤降级**；留 EL2 + skip MMU |
| E4 | 真机走 QEMU **PSCI HVC** 拉 AP | 高危复位 | `#if !BOARD_IS_VIRT` 单核 |
| E5 | 真机写 virt **GIC @0x08000000** | 写进 DRAM | Timer 改 poll；不写 GIC |
| E6 | 真机 `HalPlatformIsVirtSerialConsole=1` → virtio 探针 | 多余 | 已按 `BOARD_IS_VIRT` 关掉 |

### 3.5 缓存（当前优先怀疑）

| # | 尝试 | 现象 | 结论 |
|---|------|------|------|
| C1 | `load` + 直接 `go`（无 flush） | `Starting 0x80000` 后无 `*` | I-cache 与 MMC DMA 写入不一致 |
| C2 | `dcache flush` / `icache flush` | **unknown command**（本 2021.01 未开 CONFIG_CMD_CACHE） | 废；勿再试 |
| C3 | 高址 `bootelf` / 其它成功进核路径 | 已见到 `Boot: Mod` | 缓存问题已绕过或偶发成功；配方仍以能复现进核为准 |

`go` 不调用 `cleanup_before_linux()`；`bootelf` 的 `load_elf*_phdr` 会对每段 `flush_cache`。

---

## 4. 代码落点（避免改错层）

| 主题 | 文件 |
|------|------|
| 链接 @0x80000 | `Board/Raspberry/Pi3BPlus/link.ld`；Makefile 优先板级 `link.ld` |
| mini-UART | `BoardConfig.h` `BOARD_UART_MINI`；`Hal/HalSerial.c` |
| 入口信标 / EL2→EL1 | `BringUp/Arm64/KernelEntry.S` |
| Handoff / RAM fallback | `BringUp/Arm64/KernelHandoff.c` + `BOARD_KERNEL_LOAD` / `BOARD_RAM_*` |
| 跳过 EL2 MMU | `HAL/Arm64/PageTable.c` |
| VBAR_EL2 | `HAL/Arm64/Exception.c` |
| 无 PSCI | `HAL/Arm64/Smp.c` |
| 无 virt GIC irq | `HAL/Arm64/Hal/Hal.c` `HalTimerStart` |
| 无 virtio 登记 | `HAL/Arm64/Hal/HalDevices.c` / `HalPlat.c` |
| 摆盘说明 | `scripts/prepare-sd.sh` → `README-SD.txt`（并同步 `.md`） |

---

## 5. 现象速查

| 坏相 | 先查 |
|------|------|
| `scanning bus usb@7e980000` / `smsc95xx_eth address not set` | **可忽略**：U-Boot 扫网卡；与 Blocks 无关 |
| `Unknown command 'icache'/'dcache'` | **预期** |
| 命令缺字母（`lamm`/`otelf`）、无 `*` | **串口 RX 丢字**；改 HDMI 键盘 + `boot.scr`，勿当内核退步 |
| 绿灯闪几下灭、无 U-Boot | `config.txt` 是否又加了 `disable-bt`；U-Boot 是否不是 2021.01 stock |
| 只有红灯 / 全黑 | 固件链 / `kernel=` 是否指向坏镜像 |
| `Starting 0x0` | ELF 是否误 load 到 0x80000；改暂存 **0x20000000** |
| `Starting 0x80000` 无 `*` | 若刚用的是 `go`：改高址 bootelf；已是 bootelf 则贴 `md.b` |
| 有 `*` 无 `+` | 曾 = EL2→EL1 eret 挂；现应已撤降级。若再现则查 BSS |
| 有 `Boot:` 后复位 | 对照 §3.4；串口抓最后一行模块名 |
| HDMI 停在 Starting，CoolTerm 有 Boot: Mod | **正常**；无 FB，只看串口 |
| `no elf image at address 0x…` | 该址无有效 ELF：load 失败/文件名/分区；`ls mmc 0:1` + 看 load 字节数 |

---

## 6. 验收（2026-10-24 夜）

**过关**：`boot.scr` → Starting @ `0x80000` → 串口出现 `blocks>`，`ls` 见 `README.TXT` / `HELLO.TXT` / `VERSION.TXT`。

说明：Kernel 入口、BSS、Handoff、模块链、根 FS、串口控制台均已跑通。中间 `*`/`Boot:` 等短信标仍可能被 CoolTerm 丢光，以提示符为准。

当前配方（少打字）：

```
load mmc 0:1 0x1000000 boot.scr
source 0x1000000
```

已知限制：无 FB → HDMI 停在 Starting；MMU 仍 skip（board）；EL 未降。后续单变量再开。

### rpi-4（DTB / DRAM）

- 卡上 `pi.dtb` 的 `memory@0.reg=<0 0>` 是**模板**；`bootelf` 的 x0 是 argc 不是 DTB。  
- 新 `boot.scr`：固件 FDT → `0x02400000`；内核再试 mailbox `GET_ARM_MEMORY`。  
- 手测看：`Boot: DTB @… memory …` 或 `Boot: mbox GET_ARM_MEMORY …` + `Boot: regions=`（不要停在 Fallback）。

### rpi-5（FB 花屏 / VirtualMemory Failed）

- 花屏：U-Boot 开 D-cache，CPU 写 FB 未 `dc civac` → 修 flush + Begin/EndFront。  
- 屏上日志已通后：`Boot: Mod VirtualMemory Failed`。根因：`MapIdentity(FB@0x3e8…)` 撞 `HalPageKernelSetup` 的 **2MiB block**，`PageWalk` 遇 block 直接 return 0。  
- 修：`SplitBlockL2`（2MiB→L3×4KiB，保留属性）；横幅前整屏铺黑盖 U-Boot 残色。

---

## 7. 修订记录

| 日 | 记 |
|----|----|
| 2026-10-24 | 初稿：汇总回家手测 U-Boot/链接/UART/EL/bootelf/cache；立少换卡原则 |
| 2026-10-24 | 刷入 boot.scr；确认 HDMI Starting@0x80000 时 CoolTerm 残字属串口丢字节 |
| 2026-10-24 | **真机进 blocks>**（boot.scr + skip-MMU Kernel）；rpi-3 基本启动过关 |
