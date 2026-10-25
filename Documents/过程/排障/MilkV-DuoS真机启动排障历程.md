# Milk-V Duo S 真机启动排障历程（PR-B3 真机）

> **日期**：2026-10-25 起（首次接板手测）  
> **硬件**：Milk-V Duo S（SG2000 / CV181x）· 板载 ~8GB eMMC · PoE HAT（可选）· microSD · USB-TTL 3.3V（调试口常见丝印：GND=6、TX=8、RX=10）  
> **目标（B3 真机）**：厂商 U-Boot → Blocks 串口见 `board: milk-v-duo-s` → 进 `blocks>`（命令行靶；**无** HDMI/GUI）  
> **代码主战场**：`HAL/RiscV/Board/milk-v-duo-s/`、`BringUp/RiscV/`、`HAL/RiscV/{HalSerial,PageTable,Trap,TrapVec}.c`  
> **构建**：`cd Kernel && ./build.sh riscv BOARD=milk-v-duo-s` → `Build/HAL/RiscV/Kernel.elf`  
> **状态**：✅ **2026-10-26 串口 `blocks>`**（`skip MMU`）。Sv39 真开 / SD Block / 自动 boot 另刀。  
> **姊妹**：板包 [`NOTES.md`](../../../Kernel/HAL/RiscV/Board/milk-v-duo-s/NOTES.md) / [`README.md`](../../../Kernel/HAL/RiscV/Board/milk-v-duo-s/README.md)；归档 PR-B3 见 [`项目/路线图.md`](../../项目/路线图.md)；对照 Pi 档 [`Pi3BPlus真机启动排障历程.md`](Pi3BPlus真机启动排障历程.md)

---

## 0. 一句话（当前结论）

1. **必须用 DuoS 专用 RV SD 镜像**（如 `milkv-duos-musl-riscv64-sd`）。错刷 duo/arm64/emmc 镜像 → FSBL `RESET` / 平台断言。  
2. **拨码 RV + 插合法 SD → 优先 SD**；eMMC 上若是 ARM Linux 不会「抢跑」，只是无卡时才试 eMMC。库存固件常 **SD/eMMC 接口互斥**，勿当两块盘同时用。  
3. **不能整盘改成 3B+ 式纯 Blocks 布局**：SG2000 要厂商 **FSBL → OpenSBI → U-Boot**；删掉就进不了 `soph#`。Blocks 只往 FAT boot 丢 `Kernel.elf`，底板保留官方链。  
4. **加载**：无 `loadaddr`；用 `uImage_addr=0x81800000`（或写死该址）`fatload`/`load` + `bootelf`。链接入口 **`0x80200000`**（与 `BOARD_KERNEL_LOAD` / PHDR 一致）。  
5. **串口丢字**：USB-TTL + 延长线易 overrun；直插 + `picocom`/`tee` 可接受。CoolTerm@DB9 稳 ≠ USB-TTL 稳。  
6. **卡点已过（2026-10-26）**：开 Sv39 后任意 `ret`→`epc=0`（与缺页无关）。**对齐 Pi：`vmm: skip MMU (board)`** → 模块表跑完 → **`blocks>`**。Sv39 真开 = 后刀（可比 rpi-9）。

---

## 1. 少换卡原则（给 Agent / 自己）

| 动作 | 要不要拔卡 | 说明 |
|------|------------|------|
| 只改 U-Boot 命令 / `printenv` / `fatls` / `bootelf` | **否** | 卡在板子上即可 |
| 换 `Kernel.elf` / `HELLO.ELF`（FAT boot） | **要** | 才刷 FAT；Linux 根分区可不动 |
| 重刷整张厂商 SD 镜像 | **要** · 慎 | 仅镜像错 / 启动链坏时 |
| 把卡格式化成 Pi3B+ `bootfs+BLOCKS` | **禁止** | 会拆掉 FSBL/U-Boot |
| 同一次会话无新假设连刷 | **禁止** | 无新证据不刷 |

**刷卡前自问**：相对上次手测，卡上 **哪一个文件**变了？没变就别拔。

---

## 2. 当前正确手测配方（勿改口令）

### 2.1 SD 镜像 / 拨码

| 项 | 值 |
|----|-----|
| 镜像 | **DuoS** + **riscv64** + **sd**（例：`milkv-duos-musl-riscv64-sd*.zip`） |
| 拨码 | **RV / C906**（串口早期常见 `C.`；勿在 RV 拨码下指望 eMMC 上 ARM Linux） |
| 分区（厂商） | `boot`(FAT) + 小分区 + `rootfs`(ext4)；**保留** |

### 2.2 串口

```bash
# 口名以本机为准
picocom -b 115200 /dev/ttyUSB0 | tee ~/duo-serial.log
# 退出：Ctrl-A Ctrl-X
```

- 115200 8N1 无流控；**直插主板 USB**，少用延长线/HUB。  
- 主机若 `halt`/`poweroff` → 板子常 **立刻再进 FSBL**（无真断电）；`sync` 后拔电即可。

### 2.3 进 U-Boot（`soph#`）

厂商 `bootdelay=1`（仅 1 秒），且 `Loading Environment from nowhere` → **`saveenv` 常存不住**。

1. 先开 picocom，再上电。  
2. 一见 OpenSBI / `U-Boot 2021.10` 就 **狂按空格**。  
3. 进 `soph#` 后：

```text
printenv
fatls mmc 0:1
fatload mmc 0:1 0x81800000 Kernel.elf
bootelf 0x81800000
```

（`printenv` 无 `loadaddr` 时用 **`uImage_addr` / `0x81800000`**；官方 `sdboot` 也是该址装 FIT。）

| 看到什么 | 含义 |
|----------|------|
| `FSBL` … `RESET` / `platform.c` 断言 | **镜像/拨码不对**（错板或 ARM/RV 错配） |
| `Hit any key` 后直接 Linux | 没抢到 1 秒；重来狂按 |
| `## Error: "loadaddr" not defined` | 预期；改用 `0x81800000` |
| `load` 只打印 Usage | 地址参数空了 |
| `Starting application at 0x80200000` | bootelf 已跳进 ELF 入口 |
| `Boot: RiscV KernelMain` / `board: milk-v-duo-s` | **✅ 内核已跑** |
| `vmm: … fault path ok` 后 `epc=0` | 见 §3.5；刷含 TrapVec 修复的 elf 再测 |
| `vmm: after probe` 及后续 `Boot: Mod` | 自测返回成功（修复后期望） |
| `blocks>` | **B3 真机过关（串口壳）** |

可选：同时 load DTB 再 `bootelf -p`（板 README）；无 DTB 时走 `BoardConfig` **510MiB @0x80000000** fallback（已见日志）。

### 2.4 卡上最小文件（FAT `boot`）

| 文件 | 必须？ | 说明 |
|------|--------|------|
| 厂商 boot 文件（`fip.bin` / `boot.sd` 等） | **必须** | 启动链 |
| `Kernel.elf` | 测 Blocks 时必须 | `./build.sh riscv BOARD=milk-v-duo-s` |
| `HELLO.ELF` | 可选 | 板包 **`BOARD_HAS_BLOCK=0`**，壳里暂读不到 SD FAT；先用 **`run user`**（内嵌 hello） |

---

## 3. 方法编年（已试 · 勿回头）

### 3.1 板子识别 / 镜像选型

| # | 尝试 | 现象 | 结论 |
|---|------|------|------|
| I1 | 只有一张曾给 Pi 用的 TF | 无法同时养两套 | 后找到官方式卡；Pi/Duo **分卡** |
| I2 | 确认 Duo S + 8GB eMMC + PoE HAT | — | 硬件 OK |
| I3 | 错镜像（duo / arm64 / emmc 类） | FSBL `RESET`、`platform.c` 断言 | **废**；必须 **duos + riscv64 + sd** |
| I4 | `milkv-duos-musl-riscv64-sd` | OpenSBI `Milk-V DuoS` + Linux `mmcblk0p3` | **✅ 采用** |
| I5 | 以为 eMMC=ARM、SD=RV「不冲突就能双系统」 | — | 不抢跑，但 **有卡仍优先 SD**；接口常互斥 |

### 3.2 串口硬件 / 软件

| # | 尝试 | 现象 | 结论 |
|---|------|------|------|
| S1 | CoolTerm @ DB9 | 几乎不丢字 | 对照基线 |
| S2 | USB-TTL + **USB 延长线** + CoolTerm/picocom | 日志整段蒸发、时间戳被啃 | overrun；先换线 |
| S3 | USB-TTL **直插** + picocom + `tee` | 基本完整可排障 | **✅ 采用** |
| S4 | 换 GUI 终端「求美观」 | 屏=文件仍缺字 | 软缓冲不是主因；硬链路优先 |

### 3.3 关机 / 打断启动

| # | 尝试 | 现象 | 结论 |
|---|------|------|------|
| P1 | Linux 里 `halt` / `poweroff` | **直接又启动** | 无 PMIC 真关机；`sync`+拔电 |
| P2 | 进 Linux 后 `setenv bootdelay` | 无此命令 | `setenv` 只在 U-Boot |
| P3 | U-Boot `saveenv` | 环境来自 nowhere，常存不住 | 每次靠手速抢 1 秒 |

### 3.4 加载 Blocks（U-Boot）

| # | 尝试 | 现象 | 结论 |
|---|------|------|------|
| L1 | 整盘改成 3B+ 纯 Blocks 分区 | （未做；纸面否决） | **禁止**；会死 FSBL |
| L2 | `load mmc 0:1 ${loadaddr} Kernel.elf` | Usage / loadaddr 未定义 | 用 `0x81800000` |
| L3 | `load`/`fatload` @ `0x81800000` + `bootelf` | `Starting @ 0x80200000` + Boot 横幅 | **✅ 采用** |
| L4 | 无 DTB | `DTB Memory Missing, Fallback BoardConfig RAM` | 可接受；后补 DTB 更佳 |

### 3.5 MMU 自测后 `epc=0`（当前刀）

| # | 尝试 | 现象 | 结论 |
|---|------|------|------|
| V1 | 首刷 elf 真机 | `Sv39 on` → 故意缺页 ok → `trap cause=1 epc=0` | 死在自测返回后 |
| V2 | 反汇编 | `HalPagingEnable` **尾调用** `HalPagingSelfTest`；SelfTest 再尾调 `HalSerialWrite("\n")` | 返回链脆 |
| V3 | 读 `TrapVec.S` | S 态 `sscratch` 清零后再读填 **StackPointer=0**；若误走 U 返回 → `sp=0` → 随后 `epc=0` | **根因候选** |
| V4 | 修：S 态写入真实 SP；Enable 自测后打 `after probe` | 能见 `after probe`，**仍** `epc=0` | 不够 |
| V5 | 反汇编：Enable **先 `ldsp ra` 再 `jal zero` after-probe**（尾调用） | after-probe 的 `ret` 用的是可能已脏的 ra | 根因 |
| V6 | 真机跳过自测；禁 Enable 尾调用 | 仍 `after probe` 后 `epc=0` | 否因缺页 |
| V7 | **对齐 Pi：真机 `vmm: skip MMU (board)`**；无 MMU 时跳过 U-mode 自测 | 写 satp 后栈上 `ret` 必炸；先物理跑通壳 | **✅ `blocks>`** |
| V8 | C906：`dcache.call`→`satp`→`dcache.iall`+`sync.s`+`sfence`（T-Head MMU cache errata） | 过 probe / Mod Cpu / syscall ready | ✅ 过 probe |
| V9 | 反汇编 `epc=0x802092da` → `PhysicalMemoryAllocatePage` 内 **AMO**（SpinLock） | `cause=7` Store/AMO access fault（≠15 缺页） | C906 MAEE：叶子无 Cacheable 则 AMO 炸 |
| V10 | 恒等叶 / `NativeFromHal` 加 T-Head PMA（C\|B\|SH）；MMIO 用 SO\|SH | 对齐 Linux `_PAGE_PMA_THEAD` / NuttX | ✅ 过 U-selftest → `blocks>` |
| V11 | `run user` 无字：RiscV 链 HelloBlob stub（start/end 反序） | Makefile 嵌 `User_hello_blob.o`；滤 stub | ✅ blob 已嵌 |
| V12 | 嵌 blob 后仍无字 | C906：`HalSyncICache` 须 dcache.cva+icache.iall；`HalPageTableLoad` 刷 D | 部分（cache） |
| V13 | 仅 `run user: …` 无 size/Hello | Shell Own 吞 HalSerialWrite；**Create→Drain 被 timer 抢走用户** | ✅ size/done |
| V14 | size+done 仍无 Hello | 查 drained 计数 / write copyfail；SysWrite 旁路 WriteShell | 诊断中 |
| V15 | `drained=0` task lost | RiscV `HalTlsSetBase` 写坏 tp（CPU id）→ Activate 后 `CurrentTask` 空 | ✅ 勿写 tp |
| V16 | 假 `run user: done`、无 Hello | UP ConsoleOnly **直跑 SerialRun**（未 `SchedulerStart`）→ `CurrentTask==NULL` → CoopDrain 空退 | ✅ Create Shell + Start；真机 **Hello Ring3!** |

日志锚点（V1–V7 坏相：`epc=0`）：

```text
vmm: RiscV Sv39 on
vmm: fault path ok (probe)
vmm: trap cause=0000000000000001 epc=0000000000000000
```

日志锚点（V8 过后 / V9 坏相：`cause=7`）：

```text
vmm: RiscV Sv39 on
vmm: fault path ok (probe)
vmm: after probe
Boot: Mod Cpu
syscall: RiscV ecall (U-mode) ready
vmm: trap cause=0000000000000007 epc=00000000802092da
```

---

## 4. 代码落点（避免改错层）

| 主题 | 文件 |
|------|------|
| 板名 / UART / RAM / 能力旗 | `HAL/RiscV/Board/milk-v-duo-s/BoardConfig.h` |
| 链接 @`0x80200000` | 板级 `link.ld`（与 `BOARD_KERNEL_LOAD`） |
| UART0 `0x04140000` reg-shift=2 | `HAL/RiscV/Hal/HalSerial.c` + BoardConfig |
| Handoff / DTB fallback | `BringUp/RiscV/KernelHandoff.c` |
| Sv39 / satp / 恒等叶 | `HAL/RiscV/PageTable.c`（含 T-Head `HalSatpWrite` + MAEE PMA） |
| PTE 规范化 / 动态叶 | `HAL/RiscV/PageTableWalk.c`（`NativeFromHal` ⊕ `PTE_THEAD_RAM`） |
| 缺页自测 / U-mode 自测 | `HAL/RiscV/Trap.c` |
| trap 入口 / sret | `HAL/RiscV/TrapVec.S` |
| ConsoleOnly → 串口壳 | `HAL/RiscV/Hal/HalPlat.c`；`BringUp/Kernel.c` |
| 无 Block 时跳过枚举 | `HAL/RiscV/DeviceEnum.c`（`BOARD_HAS_*`） |
| 内嵌 hello | 壳命令 `run user`（非 SD `HELLO.ELF`） |

---

## 5. 现象速查

| 坏相 | 先查 |
|------|------|
| FSBL `RESET` / 反复重启环 | 是否 Duo**S** + **riscv64-sd** 镜像；拨码是否 RV |
| 只有 Linux、抢不到 `soph#` | 上电前开串口；OpenSBI 起就按键；接受 1s `bootdelay` |
| `loadaddr` Error / load Usage | 改 `0x81800000` 或 `${uImage_addr}` |
| 串口缺段、时间戳烂 | 拔延长线；直插；`tee` 落盘；换 FT232/CP2102 |
| `halt` 变重启 | **预期**；拔电 |
| `Starting @ 0x80200000` 后无字 | UART 基址/波特；是否错板包 elf |
| `fault path ok` 后 `epc=0` | 是否已刷含 TrapVec/after-probe / SatpWrite 的 elf |
| `syscall ready` 后 `cause=7` | C906 MAEE：叶子缺 Cacheable；AMO/SpinLock |
| 有 `blocks>` 但 `exec HELLO.ELF` 失败 | **预期**（无 SD Block）；试 `run user` |
| 想「格式化成和 3B+ 一样」 | **不要**；见 §0.3 |

---

## 6. 相关 PR / 柱（可续写）

| PR / 刀 | 用户能感到什么 | 与本档关系 | 状态 |
|---------|----------------|------------|------|
| **PR-B0～B2** | 板包约定 / 能力旗 / `BOARD=` | Duo 包能进树的前提 | ✅ 归档 |
| **PR-B3** | 首个真机命令行板：`milk-v-duo-s` 串口壳 | **本档主柱** | ✅ `blocks>` |
| **skip MMU** | 避开 satp 后 `epc=0` | 对齐 Pi；真开 → [1025 duo-mmu](../../路线图1025.md#duo-mmu) | ✅ 壳用 |
| **`run user` 真跑** | 内嵌 hello | [1025 duo-hello](../../路线图1025.md#duo-hello) | ✅ Hello Ring3! |
| **SD→FAT Block** | `exec HELLO.ELF` | [1025 duo-block](../../路线图1025.md#duo-block) | ⬜ |
| **自动 bootcmd** | 少抢 1 秒 | [1025 duo-boot](../../路线图1025.md#duo-boot) | ⬜ |
| **DTB 传入** | 少走 RAM fallback | [1025 duo-dtb](../../路线图1025.md#duo-dtb) | 可选 |
| **DeviceEnum 空引用** | 编 Duo 时少 warning | `AddMmio` 用 `BOARD_HAS_*` 包 | 小修（视 TG） |
| **不对齐** | Duo 上桌面 / HDMI | B3 **明确不做**；GUI 仍 QEMU virt | — |

Pi 侧后续刀（rpi-4…12、键鼠等）**不要**抄进 Duo 档；Duo 无 FB/DWC2，路径不同。

---

## 7. 验收（2026-10-26）

**过关（壳+MMU+hello，2026-10-26）**：`bootelf` → `Sv39 on` → U-selftest → **`blocks>`** → **`run user` → Hello Ring3!**；`ls` RES。

```text
fatload mmc 0:1 0x81800000 Kernel.elf
bootelf 0x81800000
→ board: milk-v-duo-s
→ vmm: RiscV Sv39 on … after probe
→ user: U-mode syscall ok (exit)
→ Blocks 就绪 → blocks>
→ run user → Hello Ring3!
```

已知限制：

- 无 FB / 无 SD Block → `WARN No BLOCKS.ID`、默认 RES；`exec HELLO.ELF` 待 duo-block  
- `halt` 非真关机；`bootdelay=1` 仍靠手速

---

## 8. 修订记录

| 日 | 记 |
|----|----|
| 2026-10-25 | 接板：UART、错镜像 RESET、换 duos-riscv64-sd、Linux 起来 |
| 2026-10-25 | SD/eMMC 优先级澄清；串口丢字（延长线→直插+picocom） |
| 2026-10-25 | `halt` 伪关机；抢 `soph#`；否决 3B+ 整盘布局 |
| 2026-10-25 | `bootelf` 进核；VMM 自测后 `epc=0` |
| 2026-10-26 | 初稿本文；TrapVec S-SP + after-probe；手测仍 epc=0 |
| 2026-10-26 | V6：跳过 probe 仍 epc=0 → 非缺页本身 |
| 2026-10-26 | V7：skip MMU → **真机 `blocks>`**（B3 串口壳过关） |
| 2026-10-26 | V8：SatpWrite T-Head 刷 cache → 过 probe / Cpu / syscall |
| 2026-10-26 | V9：`cause=7` @ AllocatePage AMO；V10：叶子加 MAEE PMA |
| 2026-10-26 | V10 手测：Sv39 + U-selftest → `blocks>`；`run user` 无字 |
| 2026-10-26 | V11：非 x86 嵌 hello blob（此前 stub → Size 错） |
| 2026-10-26 | V12：C906 HalSyncICache / PageTableLoad 真刷 cache；run user 面包屑 |
| 2026-10-26 | V13：OnTimer 对 HalConsoleOnly 禁抢用户；面包屑改 WriteShell |
| 2026-10-26 | V15：HalTlsSetBase 勿改 tp；V16：UP 须 SchedulerStart |
| 2026-10-26 | **duo-hello 过关**：真机 `run user` → Hello Ring3!；★ → duo-block |
