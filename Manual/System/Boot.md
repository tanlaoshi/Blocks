# Blocks 开机流程

> **目的**：冷启动链路 + 稳态四环 +「先骨后肉」读法；标出慢点红线。  
> **目录地图**：[`Blocks目录结构.md`](Tree.md)。  
> **权威代码**：`Boot/BootPkg/`、`BringUp/`、`BringUp/Modules.c`、`Services/Tasks/`、`Core/Scheduler/Start.c`。  
> **活 ★**：[`路线图1023.md`](../../Documents/路线图1023.md)（日档）。已收日档 → [`项目/进展/`](../../Documents/项目/进展/README.md)。

---

## 0. 先骨后肉（阅读心智）

**骨架** = 加电后能调度、能泵输入、能挂一层 FS/网/屏的最小闭环。  
**肉** = 桌面装饰、商店、驱动超市、字库、lwIP、多板……按事件才进。

读代码：**先跟骨架走通一圈，再按故事往钩子上挂肉**；不要从整棵 `HAL/X64/Drivers` 或 `ThirdParty/lwip` 正面硬啃。

| 偏骨架 | 偏肉 |
| --- | --- |
| BringUp Entry/Handoff/Modules、Scheduler、PMM/VMM、Process/Elf/Syscall | Student 模板、演示槽 |
| Hal* 门面、当前板真正 Probe 到的几条 | 未接设备、他板、整棵 XHCI/网卡变体 |
| Tasks 四环、Shell 分发、Gui 开窗/合成主链 | Desktop 装饰、Store 全套、Apps UI |
| Fat / VFS 主路径 | 巨型字库、lwIP 细节 |

分层规则（谁能 include 谁）与「骨/肉」正交：见技术手册体系结构；Services 里既有骨（Tasks）也有肉（Store）。

---

## 1. 冷启动总览

### 1.1 X64（课机 / QEMU / NUC）

```text
加电 → OVMF
  → Blocks Boot (BOOTX64.EFI)
       串口 → GOP → 读 Kernel.elf → ExitBootServices → jmp KernelEntry
  → BringUp/X64：KernelEntry → KernelHandoff → BOOT_INFO
  → KernelMain → ModulesRun(gModulesFull | …)
  → Create：shell / gui / worker [/ input]
  → SchedulerStart
  → 串口 “Blocks ready…”；桌面已画；Shell 窗按需打开
```

### 1.2 Arm64 / RiscV

**无 OVMF**。QEMU：`run-virt-arm` / `run-virt-riscv`（`-kernel` + DTB / OpenSBI）→ 同 `KernelMain`（模块表多为 virt / virt 桌面）。  
真机板（Pi3BPlus、Duo S）：U-Boot + DTB → 板包 UART；见板目录 README / 日档 rpi-desk。

---

## 2. 后台只有一个 worker 吗？

**专用后台任务只有一个：`worker`（`WorkerTask`）**，钉在 **BSP CPU0**。

按优先级泵（忙则 `continue`，空则 `HalCpuHalt`）：

1. `StoreJobStep` — 商店装卸  
2. `HalIwlBgPump` — iwl 后台  
3. `LwIpDhcpStep`（真机 NIC 变化时可能先 Restart）

另有协作轮询（不是第二套线程池）：

| 角色 | 任务 | 泵什么 |
| ---- | ---- | ------ |
| shell | `ShellTask` | 键鼠 dequeue、Present、串口、网轮询 |
| gui | `GuiTask` | `GuiPollMouse` / 桌面时钟 |
| input | `InputTask`（仅 **SMP≥3** → CPU2） | `HalInputPoll` |
| idle\* | 每核 | halt |

稳态 HID：SMP≥3 走 Input；否则 Gui/Shell 的 `YieldForPollInput`。  
**键鼠优先**：露光标后禁止饿死 HID；未露光标时勿狂泵鼠抢 xHCI（见仓库 `.cursor/rules`）。

---

## 3. Boot（固件侧 · X64）

| 步 | 做什么 |
| -- | ------ |
| 串口 / GOP | 最早输出；选模式、清屏 |
| 读核 | 找数据卷上的 `Kernel.elf` |
| 装载 | PT_LOAD；取入口 |
| 提示 | ACPI RSDP、可选 xHCI MMIO |
| 跳转 | ExitBootServices → `KernelEntry` |

慢点候选：大分辨率 GOP；慢介质读 ELF。

---

## 4. Kernel 早期 → 模块表

| 步 | 文件 | 做什么 |
| -- | ---- | ------ |
| Entry / Handoff | `BringUp/<Arch>/` | FB / 内存图 → `BOOT_INFO` |
| KernelMain | `BringUp/Kernel.c` | 选 Full / virt / 串口子集 |
| ModulesRun | `BringUp/Modules.c` | 只认表；以代码为准 |
| 建任务 + Start | `Scheduler/Start.c`、`Services/Tasks/` | 见 §2 |

串口 **`Blocks ready`**：Worker 末档宣告；`Desktop Ready` 仍在 Gui 模块。

### 4.1 `gModulesFull`（桌面主路径 · 阶段感）

硬脊：**PMM → VMM → Video → … → USB → FileSystem**（Usb 须在 FS 前）。

| 阶段感 | 日志名（例） | 先读 |
| ------ | ------------ | ---- |
| EARLY | Serial / PhysicalMemory / VirtualMemory | `BringUp/Initialize/Serial.c`、`Core/PhysicalMemory/`、`VirtualMemory/` |
| DISPLAY | Video | `BringUp/Initialize/Video.c` |
| PLATFORM | Cpu / Driver / Smp | `Initialize/Cpu.c`、`Core/Driver/`、`HAL/…/Smp*` |
| INPUT | USB | `BringUp/Initialize/Usb.c` |
| SESSION | Scheduler / Console | `Core/Scheduler/`、`Services/Shell/Console/` |
| Services | FileSystem / Gui / Network | 各 Services 下 |

其它表：`gModulesVirt`（串口-only）、`gModulesVirtDesktop`（virt 桌面）。细节以 `Modules.c` 文件头为准。

第一次顺着读时，先跳过点菜单才进的 `*Ui`；驱动种类看 `Include/Driver/DriverBoot.h` 登记序。

---

## 5. 稳态四环 + 钩子

```text
Input（够核时）── drain HID/XHCI
Gui ───────────── 唯一 GuiPollMouse；合成 / 栏
Shell ─────────── Present / 串口 / 键；blocks>
Worker ────────── MSC/钟、扫盘、Store、网后台…
```

| 钩子 | 骨架动作 | 挂上的肉（例） |
| --- | --- | --- |
| 鼠标 | `GuiPollMouse` → 点击 | 图标、菜单、开窗 |
| 开窗 | `GuiOpen*` | Settings / Files / Store / USER |
| 命令行 | `exec` / `run user` | Process/Elf、stdout 契约 |
| 热拔 | Worker → MSC | Mount、Fat、文件 App |
| 商店 | Worker Store* | Catalog / 装包 |
| 中断/驱动 | Hal 登记表 | 具体 NIC / 键鼠 / 块 |

推荐读法：

1. `BringUp/Kernel.c` → `Modules.c` → `Scheduler/Start.c` → Tasks 文件头  
2. Gui / Shell / Worker 各跟一圈空转  
3. 再按日档 ★ 挂肉；事故再钻某驱动一角  

---

## 6. 加速红线与慢点（备忘）

**勿为加速拆掉**：

- Usb **先于** FS  
- 无 HID / 无 NIC → **软退**，不挡桌面  
- Worker **不**在 Gui 里 Step 装卸（卡鼠标）  
- 无线 **永不串口打 PSK**  
- 键鼠优先（见仓库规则）

历史慢点（多已进 PR-BOOT-fast 归档）：Gui 墙纸/图标过早读盘、MSC 重试、iwl Claim 挡 FS、GOP 无谓 SetMode。  
测量：有 QEMU 时可用 OpenBox `measure-boot` 一类脚本；真机以 NUC 墙钟为准。排期认**日档 ★**，大文件路线图只留史。

---

## 7. 骨架自测清单

能口头答上即可：

- [ ] Boot 如何交到 `KernelMain`，模块表大概跑哪几类 Init  
- [ ] 谁 Create 了哪几条任务、钉在哪核（含 Input 门槛）  
- [ ] Gui / Shell / Worker 稳态各泵什么；谁**不**泵鼠标  
- [ ] `exec` / 开窗如何挂上 Process  
- [ ] 块/键鼠是否只经 Hal 门面（业务直 `#include` 驱动私头 = 分层焊死）  

---

## 8. 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-10-23 | 合成：原「开机流程与加速」+「骨架与肉」；提示符 / 产品名对齐 Blocks。 |
