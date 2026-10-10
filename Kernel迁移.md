# Blocks · Kernel 迁移

> **工作区**：`~/Blocks` · `git@github.com:tanlaoshi/Blocks.git`  
> **对照源**（只读）：`~/tanlaoshi/edk2/ToyKernel`（路径冲突以 Blocks 为准）  
> **接棒**：[`Boot迁移.md`](Boot迁移.md) §1.5 已收口。彻底干净 ≠ smoke，见下文「终局」。

---

## 为何迁（钉死）

1. **现网已经跑通**——功能多、路径经你测过无数次。命名、调用链、行为以 **`~/tanlaoshi/edk2/ToyKernel` 源码** 为准，不是凭文档或想象另写一套。  
2. **迁到 Blocks 的目的（两件都要）**：  
   - **还债**：清技术债、偷懒方案、模块化不足、可读性差、分层不清，以及藏在代码里你还没掌握的问题。  
   - **搞懂**：现网是很快堆通的，当时没时间把设计吃透。迁移是把**设计意图、原理、执行步骤、调用链**写清楚、理清楚的机会——尤其开机模块表、驱动分层、注册制（Shell/Driver/…）等你还没完全吃透的部分。  
3. **怎么干（快且能学）**：打开现网对应 `.c/.h` → **好的搬过来** → 同刀清债 + **按注释规范写清「做什么/谁调用/前后文」** → 主链变动同步 [`调用链.md`](调用链.md) → 编过+smoke。禁止另写试验路径、禁止扫文档充数、禁止大而空的「规划剧场」。迁完一块是一块；USB/WiFi 等硬路径尤其跟源走。  
4. **现在没有发布、没有用户**——大改窗口就在现在；固化后再拆就难了。  
5. Blocks 终局须 **比现网强很多**：能力 **≥** 现网路线图 **§1**（可加码）+ 结构/债明显更好。否则 TOKEN 无意义。  
6. **终局交付物**——两份都要齐，缺一不算迁完：  
   - **权威文档**：[`Blocks系统说明.md`](Blocks系统说明.md)——每机制/子系统必须写清 **职责 · 设计原理 · 运行逻辑 · 依赖关系**（四栏）；现写已落地、随时补。卫星：[`调用链.md`](调用链.md)、[`积木原则.md`](积木原则.md)、本文差缺表。  
   - **代码注释**：按 [`代码注释规范.md`](代码注释规范.md)（做什么 / 谁调用 / 前后文）。  
7. **禁止**：不看现网瞎猜重写；只改代码不改权威文档；子系统节只写「干什么」无原理/依赖；扔功能换瘦壳。新发现记 [`现网待迁问题盘点.md`](现网待迁问题盘点.md)。

---

## 迁移钉条

| # | 条文 |
| - | ---- |
| 1 | **现网是真相源**：行为/主路径/命名先读现网已跑通代码；**好的就用，不好的才改**。禁止另猜一套「大概能跑」的实现，禁止为「试试」另开路径烧 TOKEN。 |
| 1a | **USB / WiFi（及同类硬路径）**：现网已上百刀跑通——迁入时**跟源码走**，只清债/分层/命名；**禁止**重写协议栈、重猜描述符/环/时序、用实验分支顶替现网路径。 |
| 2 | **终局**：能力 ≥ 现网 §1 + 结构优于现网 + **权威文档四栏齐** + **代码注释齐**。禁止 smoke / 缺文档四栏 / 缺注释宣布收官。 |
| 3 | **迁=还债窗口**：迁到之处一并清（反依赖、超 300、短名、`*Init`、缺注释、Hal 根堆驱动、偷懒方案）；并挖隐藏问题，对标业界常规修。实现可略薄。 |
| 4 | **规范**：现网《代码可读性规范》《开发命名规范》+ 本仓 [`代码可读性盘点.md`](代码可读性盘点.md) / [`代码注释规范.md`](代码注释规范.md) / [`积木原则.md`](积木原则.md)。命名跟现网全词习惯走，坏名同刀改。 |
| 5 | **分层单向**：Boot → HAL → Core →（Services 形）→ User；禁止 HAL→Core 政策头。 |
| 6 | **驱动一设备一夹**：`Hal/<Arch>/Drivers/<Device>/`；迁入时补完现网根遗留，禁止再往 Hal 根堆设备。 |
| 7 | **目录即命名空间**：夹内不叠目录名；同名主文件保留；`Hal*` 层前缀例外。见 [`命名规范盘点-Blocks.md`](命名规范盘点-Blocks.md)。 |
| 8 | **字体/课感**：汉字观感必须过关；可点阵主路径，禁止糊弄式 fallback 当「齐」。 |
| 9 | **文档随时可改**；**无明确 JX 不改代码**（`Kernel/` `Boot/` `User/` `Runtime` 产物等）。 |
| 10 | **每刀交付必须交代**（见下节）；刀可小，但必须有结构/债价值；命名与可读性**同刀过关**，禁止交完再逼你重构。 |
| 11 | **权威文档标杆**：动子系统必须更新 [`Blocks系统说明.md`](Blocks系统说明.md) 对应节的 **职责/设计原理/运行逻辑/依赖关系**；代码「谁调用」；主链变改 [`调用链.md`](调用链.md)。 |

### 每刀交付（JX 收口必写）

| 项 | 写清楚 |
| -- | ------ |
| **迁/动了哪** | 现网哪棵子树 / Blocks 哪几个文件（路径） |
| **改了什么** | 行为零变还是修了哪类债（拆文件、分层、命名…） |
| **权威文档** | 已更新 `Blocks系统说明` 哪一节四栏（职责/原理/运行逻辑/依赖）；无更新则刀未完 |
| **对标业界** | 例如：按子系统分编译单元、命令表注册模式、单向依赖、一设备一夹 |
| **比现网好在哪** | 具体一点（现网同债若已更好则写「对齐现网已拆法并补全词命名」） |
| **如何测试** | 编译命令 + smoke/抽测步骤 |
| **自查** | `命名 OK / 行数 OK / 注释谁调用 OK`；主链变则改了 [`调用链.md`](调用链.md) |

---

## 0. ★ 当前刀

| 项 | 值 |
| -- | -- |
| **★** | **PR-B-read** · 拆超标 Core（Shell/Gui/Fat ✅；余 Tcp） |
| 排队 | **PR-B-hal** → **PR-B-seq** → **现网切片迁入**（读代码清债 + 补 §1）→ 后置真机细项 |
| 刚收官 | **K54** `smoke-boot`（**仅冒烟**；≠ ≥现网） |
| 相关文 | [`现网待迁问题盘点.md`](现网待迁问题盘点.md) · [`代码可读性盘点.md`](代码可读性盘点.md) · [`积木原则.md`](积木原则.md) |

### 暗号

| 暗号 | 含义 | Agent |
| -- | -- | -- |
| **JX** | 做文首 ★（或点名刀） | **先读现网对应源** → 迁入/拆分并清债 → 本地验收；**禁止瞎猜另写**；**不** commit/push |
| **TG** | 入库 | **只 commit**；推 ★ / 债表 / 修订记录；**宜**再 **TS** |
| **TGJX** | 先 TG 再 JX | 不 push |
| **TS** | 推远程 | `git push` |
| **GD** | 归档已完成专节到文末 | 不改代码、不擅自 TG |

> TG 必须同步：文首 ★、未完成表状态、修订记录；相位星勿停旧刀。远程旧星 = 忘记 TS。

### GD 排版

| 区 | 内容 | 位置 |
| -- | ---- | ---- |
| **未完成** | 为何迁、钉条、★、三轨未完、当前刀规划 | 文首 |
| **已完成** | K0–K54 演示轨专节与相位全表 | 仅 [【归档】](#sec-gd) |

---

## 未完成 · 三轨

> **骨架演示轨**（K0–K54）已进归档——那是「能开机演示」，**不是**终局。  
> 终局 = 下表三轨都收官（能力齐 + 结构债清 + 未迁大头按质迁完）。

### 轨 A · 现网未迁（大头）

| 要求 | 说明 |
| -- | ---- |
| 怎么干 | 按切片打开现网已跑通 `.c/.h`（命名/路径现成），**好的搬过来用**；同刀只改债/分层/可读/隐藏坑；对标业界常规，实现可略薄。 |
| 禁止 | **不看现网瞎猜 / 乱试**（尤 USB、WiFi）；原样粘贴偷懒债；HAL→PhysicalMemory 等反依赖；Drivers/Services 根散落再搬；文档扫描冒充 review |
| 能力 | 迁入块须能支撑 §1；主路径对齐现网已测通行为，不是「另写一版差不多」 |
| 登记 | [`现网待迁问题盘点.md`](现网待迁问题盘点.md) 只收**读代码新发现**（路径:行号） |

### 轨 B · 能力 ≥ 现网 §1（差缺）

对照现网 `ToyKernel/Documents/路线图.md` §1。**齐**≈课感同级；**薄**=明显矮；**缺**=基本未落地。  
**禁止**在下表仍有「缺/薄」时写 B/彻底干净收官。

| 面 | 现网 §1（摘要） | Blocks | 状态 |
| -- | -------------- | ------ | ---- |
| 桌面 | 标题栏/拖/重叠；图标开 Shell/Settings/Files | 有；合成/手感弱 | **薄** |
| 主题 | DB+THEME.CFG；色/点阵/分辨率 | 有；边角待齐 | **薄** |
| 存储 | GPT+FAT（LFN、`.`/`..`、多卷、RES） | 基本有；LFN/RES 等未齐 | **薄** |
| 文件浏览器 | 进目录、预览、ELF、删/建/改名 | 弱预览/深度 | **薄** |
| 用户态 | COW fork、pipe/dup/brk/kill、CRT、`.so` | 有 fork/exec/文件；COW/pipe/so 弱或无 | **缺/薄** |
| 语言 | UTF-8+CJK+lang | CJK/TTF/lang（加码中） | **齐/加码**（观感仍须过关） |
| 网络 | 默认 lwIP；dns；NETDEMO；DHCP/tray… | virtio；lwIP 非默认；DHCP/tray 缺 | **薄** |
| SMP | 演示级 AP / S-ap | 基本无 | **缺**（勿默认扔掉；降级须你点头） |
| 真机 NUC | U 盘、xHCI、键鼠、电源、NVMe/AHCI… | 未做 | **后置**（不挡先齐 QEMU §1） |
| 输入 | 键鼠可用（HID 对标） | HID+PS/2 双栈缠绕 | **薄**（要门面化） |

### 轨 C · Blocks 已进树结构债（★ 现做）

> 出自 [`代码可读性盘点.md`](代码可读性盘点.md)。**清完 ≠ 能力收官**，仍须继续轨 A/B。

| 状态 | PR | 现状 | 验收 |
| ---- | -- | ---- | ---- |
| **★** | **PR-B-read** | Shell/Gui/Fat ✅（Fat 最大 **152**）；Tcp **503** | 四处均 ≤300；`smoke-boot` + 抽测 |
| 排队 | **PR-B-hal** | Virtio/PS2/xHCI 仍在 Hal 根；`*Init`；短名 | 进 `Drivers/<Device>/`；`*Initialize`；全词 |
| 排队 | **PR-B-seq** | KernelMain 非顺序表 | 薄顺序表；开机到桌面 |

咬合：禁止往超标文件继续堆；新驱动第一天进夹且 ≤300。

---

## 当前 ★ 规划 · PR-B-read

**一句话**：拆 Core 四处超标到 ≤300，零行为优先。对标现网已拆法，禁止瞎猜另写。

| 项 | 定调 |
| -- | ---- |
| **本刀已做** | Shell / Gui / Fat（对标现网 FatDirSlot+FatFileWrite/Delete → `FatSlot/Write/Mkdir/Rm/Rename`） |
| **余下** | Tcp 按能力拆（对标现网已拆 TCP 文件，好的照搬） |
| **不做** | 改协议/UI 行为；Hal 搬家（→PR-B-hal）；冒充 §1 收官 |
| **验收** | `wc -l`≤300；`./smoke-boot.sh`；抽测 help/ls/write、拖窗、tcp/udp |
| **自查** | 命名全词、无新缩写；交付附「每刀交付」六项 |
| **之后** | 四处齐 → ★→PR-B-hal |

### 后置（不占当前 ★）

| 项 | 说明 |
| -- | ---- |
| 真机 NUC 细项 | QEMU §1 齐后再开；约定 #4 对范围 |
| Arm/RiscV 全桌面 | 保持可编；不对齐 B 终局 |
| 更大字库/设计器/声卡/iGPU | 加码或后置；最小课感字体须先过关 |
| SMP 细项 | 若 §1 要演示级，排能力轨，勿默认可扔 |

---

## 1. 终局形态（摘要）

```text
加电 → OVMF → BOOTX64.EFI → Kernel.elf
  → KernelMain → 模块表 → GOP 桌面 / Shell
  → 能力 ≥ 现网 §1，结构明显强于现网
```

入口：现网 `run-split` / `smoke-boot` ↔ Blocks `Runtime/run.sh` / `smoke-boot.sh`（冒烟必要非充分）。

| 档 | 含义 | 状态 |
| -- | ---- | ---- |
| **A** | 接棒 + 模块表 + 加厚到桌面骨架 | ✅ 演示轨 |
| **B** | 能力 ≥ §1 + 结构债清 + 未迁按质迁完 | **未完** |
| **C** | 真机 NUC 细项等 | 排队 |

模块表（Blocks）：`Serial → Memory → VirtualMemory → Driver → Video → Cpu → USB → FileSystem → Network → Gui → Scheduler → Console`  
（相对现网：无 SerialEarly/Smp；序为 Memory→VMM→Driver。）

细则与积木约束见 [`积木原则.md`](积木原则.md)；演示轨历史见文末归档。

---

## 2. 目录预期（随刀）

```text
Kernel/
  Core/<Module>/     # 目录即命名空间；同名主文件=编排
  Hal/<Arch>/Drivers/<Device>/   # 一设备一夹（必须）
  Hal/<Arch>/        # 仅门面薄文件 / Entry / Handoff
  Hal/Common/        # 跨 Arch 门面与 Stub
  Include/Core|Hal/
  ThirdParty/        # 例外区
```

---

## 3. 相关入口

| 文档 | 用途 |
| ---- | ---- |
| [`现网待迁问题盘点.md`](现网待迁问题盘点.md) | 读代码新发现（路径:行号）；边迁边记 |
| [`代码可读性盘点.md`](代码可读性盘点.md) | Blocks 已进树超标/命名 |
| [`积木原则.md`](积木原则.md) | 分层 / Ops / 一设备一夹 |
| [`Boot迁移.md`](Boot迁移.md) | 接棒边界 |
| [`Blocks系统说明.md`](Blocks系统说明.md) | **人读主文**：机制+子系统原理/步骤；已完成先写，随时补 |
| [`调用链.md`](调用链.md) | 上电→模块表→桌面/Shell 逐步调用；TG 改主链必改 |
| [`积木原则.md`](积木原则.md) | 分层 / Ops / 一设备一夹 / 注册边界 |
| 现网路线图 §1 | 能力验收底线 |

---

## 4. 修订记录

| 日期 | 说明 |
| ---- | ---- |
| 2026-10-08 | 初稿：KernelMain 以后计划 + PR-K0…K9+；★ = K0 |
| 2026-10-08 | PR-K0 落地；★ → K1 |
| 2026-10-08 | 挂钩 [`积木原则.md`](积木原则.md)；原则 #1 = 积木优先 |
| 2026-10-08 | 工作区改名 **Blocks**；Kernel 已迁源码补【初学者】注释 |
| 2026-10-08 | 非迁移文档去掉「从哪迁来」表述；`Ramfb`→`RamFrameBuffer` |
| 2026-10-08 | TG：K0+三架构 HalSerial+Blocks 命名+`Hal/Common`；★ 仍为 K1 |
| 2026-10-08 | 原则钉「最小子集」；K1 收窄为 X64 LFB DrawPixel/FillRect |
| 2026-10-08 | JX：K1 代码落地（待 QEMU 手测色块） |
| 2026-10-08 | Runtime：`Esp`/`RootFs`/`Fw` + `run.sh`；headless 见 FB/self-test/park |
| 2026-10-08 | TG：K1 + Runtime QEMU；手测黄块 ✅；★ → K2 |
| 2026-10-08 | TG：K2 Font 8×8 + DrawString；左上角 `Blocks K2` ✅；★ → K3 |
| 2026-10-08 | TG：K3 HalSerialGop 屏上 boot 字；`SCREEN_LOG=1` ✅；★ → K4 |
| 2026-10-08 | TG：K4 最小位图 PMM；`[Mod] Memory` / self-test ok ✅；★ → K5 |
| 2026-10-08 | `HalCapability` / `HalVideoStub` 从 `Core/` 迁入 `Hal/Common/`（Hal 前缀不再混在 Core） |
| 2026-10-08 | TG：K5 Driver 壳 + VMM 认领 EarlyIdentity；★ → K6（含 K6 规划） |
| 2026-10-08 | TG：K6 Video 背缓冲 + BootInfo 分层；★ → K7（含 K7 规划） |
| 2026-10-08 | BootPkg：非入口源文件去掉 `Boot` 文件前缀（`Serial`/`Video*`/`LoadKernel`/…） |
| 2026-10-08 | TG：K7 Cpu GDT/IDT；`KernelMain.c`→`Kernel.c`；`Module.c`→并入 `KernelModules`；★ → K8 |
| 2026-10-08 | 钉原则 §1.2#7：路径已有 Kernel 则去文件名冗余前缀；`KernelModules`→`Modules` |
| 2026-10-08 | TG：K8 Scheduler+Console；去开机自检色块；★ → K9（FS 识盘） |
| 2026-10-08 | TG：K9 FileSystem（Boot `BLOCKS.ID` handoff）；★ → K10（USB） |
| 2026-10-09 | TG：K10 USB（`XhciBase` handoff / 窗内 CAP）；★ → K11（Network） |
| 2026-10-09 | TG：K11 Network（PCI class 0x02）；★ → K12（Gui） |
| 2026-10-09 | TG：K12 Gui 桌面壳；钉 K13…K20「加厚到桌面」排队；★ → K13（Map MMIO） |
| 2026-10-09 | TG：K13 VMM MapMmio + Usb 读 CAP；★ → K14（xHCI 端口） |
| 2026-10-09 | 表序：`Memory → VirtualMemory → Driver`（Driver 不再夹在 PMM/VMM 之间） |
| 2026-10-09 | TG：K14 HalXhci 复位+端口 CCS；★ → K15（PS/2 键入） |
| 2026-10-09 | TG：K15 HalPs2Keyboard + Console 双路输入；★ → K16（内核读 BLOCKS.ID） |
| 2026-10-09 | 画字落点：`Core/Font.c`→`Hal/Common/HalFont.c`（字库头进 `Include/Hal/`）；**后刀**整套 Theme/TTF 时再拆 **Font 积木**，调用方仍只认 `HalVideoDrawString*` |
| 2026-10-09 | TG：K18 HalTimer/LAPIC tick + HalFont 落点；★ → K19（HELLO.ELF） |
| 2026-10-09 | 钉协作：X64 主力 / 注释加厚 / [`调用链.md`](调用链.md) / 驱动先讨论；★ 仅 TG 推进；K19 JX ✅ 待 TG |
| 2026-10-09 | TG：K19 HELLO.ELF + 调用链文档；★ → K20（virtio-net） |
| 2026-10-09 | TG：K20 legacy virtio-net TX/RX + Console 跟手；★ → 后置拆刀 |
| 2026-10-09 | 钉 K21–K28 课感桌面主轨 + K29–K34 可选；★ → K21（Font/Theme 最小）；分档 A/B/C |
| 2026-10-09 | **终局改钉对标现网**：K21–K52 分 P1–P5；课感 B 档废止；约定 #5；★ 仍 K21 |
| 2026-10-09 | TG：K21 Font+Theme；★ → K22（Shell 命令表） |
| 2026-10-09 | TG：K22 ShellCommand + FontDraw/HalBootFont/反债清扫；★ → K23（ls/cat） |
| 2026-10-09 | 画字契约收干净：`FontDraw*`；`HalVideo.h` 不再声明 DrawString* |
| 2026-10-09 | 反债清扫：Hal 头只留已实现；HalDma；删现网预抄 Hal.h/Devices/Console；EarlyIdentity 公开头 |
| 2026-10-09 | TG：K23 FatVolume/FatDirectory + Shell ls/cat；★ → K24（write/mkdir/rm） |
| 2026-10-10 | 补齐 P1 相位表：K21 ✅（此前漏标仍 ★）；口诀与 TG 同步约定 |
| 2026-10-10 | 排期：汉字/TTF 提前为 **P1b（K26–K28）**；窗管起顺延；主轨至 K54；TTF 最小光栅进 B、完整字库仍后置 |
| 2026-10-10 | TG 补齐：K24–K30 ✅（P1 写盘/系统令、P1b 汉字·TTF·lang、P2 单窗+拖焦点+鼠手感）；★ → **K31**（关窗/重画桌面）；JX K31；同步 `调用链.md` |
| 2026-10-10 | TG：K31 ✅ 关窗不花屏 + 鼠停卡（双写光标/Aux 半包）；★ → **K32**（双窗+Z 序）；JX K32 |
| 2026-10-10 | TG：K32 ✅ 双窗 Z 序（手测重叠/抬升/拖关通过）；★ → **K33**（桌面图标+双击开 Shell） |
| 2026-10-10 | TG：K33 ✅ 桌面 Shell 图标双击；Core 按模块表分目录；★ → **K34**（Settings/Theme）；JX K34 |
| 2026-10-10 | TG：K34 ✅ Settings 改色立即可见；★ → **K35**（Files 列目录/开 ELF）；JX K35 |
| 2026-10-10 | TG：K35 ✅ Files 窗 ls/点 ELF；★ → **K36**（开始菜单/任务栏）；JX K36 |
| 2026-10-10 | TG：K36 ✅ 底栏开始菜单（顶栏仅 Blocks；钮宽吃下 Start）；★ → **K37**（THEME.CFG） |
| 2026-10-10 | TG：K37 ✅ THEME.CFG/mode + 盘读 CJK18 + Layout + QEMU 1440×900；★ → **K38**（ICMP ping）；JX K38 |
| 2026-10-10 | TG：K38 ✅ ICMP `ping`（`Ping`）；★ → **K39**（UDP）；TS 推远程 |
| 2026-10-10 | JX：K39 ✅ UDP bind/send/recv + 本机回环；`Ip`/`Udp`；headless `udp: sent`/`udp: recv … hello-k39`；待 TG |
| 2026-10-10 | TG：K39 ✅ UDP + Shell；顺手图标标签居中；★ → **K40**（TCP 最小） |
| 2026-10-10 | JX：K40 ✅ 单连接 TCP echo + `tcplisten`/`tcpconnect`；hostfwd :15000；待 TG |
| 2026-10-10 | TG：K40 ✅ TCP 最小；★ → **K41**（lwIP + `lwip on`） |
| 2026-10-10 | JX：K41 ✅ 嵌入 lwIP + `lwip on`/`status`；ping 走 lwIP；待 TG |
| 2026-10-10 | TG：K41 ✅ lwIP + `lwip on`；去 Toy 前缀；卷标 `BLOCKS.ID`；★ → **K42**（DNS/Configuration） |
| 2026-10-10 | TG：K42 ✅ DNS/`Configuration` + Shell `dns`/`net`；★ → **K43**（NETLIB/NETDEMO） |
| 2026-10-10 | TG：K43 ✅ `NETDEMO.ELF` + socket syscall/`LwIpSock`；★ → **K44**（多卷前缀）；TS 推远程 |
| 2026-10-10 | **GD**：K0–K43 已收官专节迁文末 【归档】；暗号表补 TS/GD |
| 2026-10-10 | TG：K44 ✅ GPT/`Volume` 多卷前缀 + `vols`；★ → **K45**（Files 删/建/改名） |
| 2026-10-10 | TG：K45 ✅ Files New/Del + Shell `mv`/`FatRenamePath`；光标前缓冲；★ → **K46**（DB KV） |
| 2026-10-10 | 钉**迁移钉条**：可读/模块化/能力≥现网；守命名·可读规范；改到还债；单向分层；驱动一设备一夹 |
| 2026-10-10 | 约定：无明确 **JX** 不改代码；盘点 PR 入文首 **待执行**；**GD** 排版=未完成在前、已收官只文末；K44/K45 专节迁归档 |
| 2026-10-10 | **GD**：P1–P3/K13–K20 全表迁归档；文首只留 P4 余量/P5/后置/待执行/K46 规划 |
| 2026-10-10 | TG：K46 ✅ `BLOCKS.DB`/`dbget`/`dbset`；★ → **K47**（Store 装包） |
| 2026-10-10 | TG：K47 ✅ Store/`STORE.CAT`/`store list|install`；目录即命名空间（Network/Gui/…）+ 产物镜像源码树；★ → **K48**（StoreUi/Job） |
| 2026-10-10 | TG：K48 ✅ StoreJob/StoreUi + Z 序修 Store 开窗；★ → **K49**（Ring3 独立页表） |
| 2026-10-10 | TG：K49 ✅ VirtualMemorySpace + exec 切 CR3；★ → **K50**（fork/wait 最小） |
| 2026-10-10 | TG：K50 ✅ fork/wait + FORK.ELF（C/P/done）；★ → **K51**（CRT/CAT/WRITE） |
| 2026-10-10 | TG：K51 ✅ SYS_OPEN/文件 fd + CAT/WRITE.ELF；★ → **K52**（USB HID） |
| 2026-10-10 | TG：K52 ✅ UsbHid + qemu tablet/kbd；★ → **K53**（MEMORY/SCHEDULER_OPS） |
| 2026-10-10 | TG：K53 ✅ MEMORY_OPS/SCHEDULER_OPS + PhysicalMemory 拆位图 + UsbHid 全词/Internal；★ → **K54**（smoke） |
| 2026-10-10 | TG：K54 ✅ `Runtime/smoke-boot.sh`；★ 曾误推后置 C / 误标 B 收官 |
| 2026-10-10 | 排期：可读性债未完；★ → **PR-B-read**；P5 表 K49–K54 演示轨 ✅ |
| 2026-10-10 | **口径纠错**：钉条 #1 = 落地 **≥ 现网 §1**；K54/smoke **不是** B 收官；文首钉 §1 差缺表；禁止再扔功能当迁移完成 |
| 2026-10-10 | **动机钉死**：迁 = 因现网可读/模块化/债不满而重落地；未发布窗口大改；Blocks 须**比现网强很多**（能力+结构），否则无意义 |
| 2026-10-10 | [`现网待迁问题盘点.md`](现网待迁问题盘点.md)：作废文档扫描版；只收读代码新发现（路径:行号）；边迁边读边改 |


| 2026-10-10 | **GD**：P4/P5 全表 + K46–K54 已收官摘要迁文末 【归档】；文首只留 ★=PR-B-read / 债表 / 后置 C 排队 |
| 2026-10-10 | **文首重组织**：按「为何迁 / 未发布窗口 / ≥现网且更强 / 读代码边迁边改 / 不照搬」重排；三轨未完；废除假 B 收官口径 |
| 2026-10-10 | **原则重申**：现网已跑通=真相源（命名/路径现成）；迁=清债+模块化+可读+分层+挖隐藏问题；对标业界；实现可略薄；**禁止瞎猜另写** |
| 2026-10-10 | 钉 **每刀交付** 六项；JX：**PR-B-read/Shell** — 对标现网 ShellCommands 分文件，865→多文件均≤239；行为零变；★ 仍 PR-B-read（Gui/Fat/Tcp） |
| 2026-10-10 | TG：ShellCommand 入子夹+文档口径；★ 仍 **PR-B-read** |
| 2026-10-10 | JX：**PR-B-read/Gui** — 对标现网 GuiCursor/GuiPointer → `Gui/Cursor.c`+`Pointer.c`；453→≤212；★ 仍 PR-B-read（Fat/Tcp） |
| 2026-10-10 | 钉条：**好的就用、不好的改**；USB/WiFi 等硬路径禁止乱试重写 |
| 2026-10-10 | TG：Gui Cursor/Pointer + 好用现网口径；★ 仍 PR-B-read |
| 2026-10-10 | JX：**PR-B-read/Fat** — 对标现网 FatDirSlot/Write/Delete → Slot/Write/Mkdir/Rm/Rename；路径零变；★ 仍 PR-B-read（Tcp） |
| 2026-10-10 | **全树命名+注释清扫（Kernel 第一方）**：Gui（HitBtn→ButtonHit 等）/ FileSystem / Console / Network / Memory…Hal（`*Init`→`*Initialize`）；点阵/第三方跳过；余超 300：Tcp/Ip/Lwip/DataBase/部分 Hal |
| 2026-10-10 | 钉：**借迁移搞懂**（模块表/驱动分层/注册制/调用链）；交付加「设计/原理」；挂钩 [`调用链.md`](调用链.md) |
| 2026-10-10 | 终局交付钉死：**详细文档（人能看懂）+ 代码详细注释** 两份都要，缺一不算迁完 |
| 2026-10-10 | 开写 [`Blocks系统说明.md`](Blocks系统说明.md)：机制三件套+已落地子系统；约定随时补 |
| 2026-10-10 | **权威文档**：强制四栏（职责/设计原理/运行逻辑/依赖）；升格为迁移标杆 |
| 2026-10-10 | TG：Fat 拆分+全树注释命名清扫+权威文档；★ 仍 **PR-B-read**（余 Tcp 等超标） |

---

## 5. 【归档】 <a id="sec-gd"></a>

> 暗号 **GD**：本节 = **已完成**专节唯一落点（骨架演示轨 K0–K54 等）。  
> 文首只留：**未完成**（为何迁、★、三轨未完、当前刀规划）。**禁止**把 K54/smoke 写成 ≥现网收官。  
> Ctrl+F `### K` 查历史；**勿再当 JX**。


### 已完成相位表（GD · 摘要）

### 加厚到桌面（K13+ 排队 · 一句话）

> 下表为**骨架加厚**（已收官演示轨）。**真正终局**见文首（能力≥§1 + 结构更强）。**JX 只认文首 ★**。

| PR | 一句话 | 为何排这里 | 验收（口述） |
| -- | ------ | ---------- | ------------ |
| **K13** ✅ | VMM：把高址 MMIO（如 xHCI BAR）映进页表 | K10 只能 handoff，读寄存器会挂 | `VMM: map mmio ok`；`Usb: … cap=` ✅ |
| **K14** ✅ | USB：xHCI 复位 + 端口状态（仍不 HID） | 有 MMIO 才能摸控制器 | `Usb: reset ok` / `port N CCS=` ✅ |
| **K15** ✅ | 键入最小（QEMU 先 PS/2；HID 后刀） | 桌面要键入；串口壳可先吃键 | GTK 窗按键进 `Blocks>` ✅ |
| **K16** ✅ | FileSystem：内核侧 Block+FAT 读 `BLOCKS.ID` | 去掉对 Boot 扫卷的依赖 | `Fs: BLOCKS.ID ready (kernel)` ✅ |
| **K17** ✅ | Gui：鼠标光标 + 桌面点击反馈 | 从「画皮」到可指点 | 光标移动；点击顶栏有串口/屏反馈 ✅ |
| **K18** ✅ | Scheduler：LAPIC 定时器 + 协作/轻抢占 | 现网桌面要节拍；Console 可 yield | 周期性 tick 日志或光标闪；shell 仍活 ✅ |
| **K19** ✅ | 用户态：加载并跑一个 `HELLO.ELF` | 到桌面课感；RootFs 已有 ELF | 串口见 hello；进程退出回 `Blocks>` ✅ |
| **K20** ✅ | Network：virtio-net 最小收发（如 ARP/ping 一侧） | 表上有网卡但不会说话 | 一次 TX/RX 成功日志；不接 lwIP 全栈 ✅ |

### 对标现网 · 已完成相位

#### 相位 P1 · 主题 / Shell / FS 面（K21–K25）

| PR | 一句话 | 现网对照 | 验收（口述） |
| -- | ------ | -------- | ------------ |
| **K21** ✅ | Font 积木 + Theme 色板最小 | D 族 Theme/Font 入口 | 桌面/顶栏色来自 Theme；契约 `FontDraw*`（开机屏 `HalBootFont`） |
| **K22** ✅ | Shell 命令表 + `help`/`clear`/`echo` | `ConsoleRegisterBuiltins` | 命令可扩展；未知命令提示 |
| **K23** ✅ | Fat：`ls` / `cat` 根目录 | FS 族只读 | 见 `BLOCKS.ID`/`HELLO.ELF`；文本可读 |
| **K24** ✅ | Fat：`write` / `mkdir` / `rm` 最小 | FS 读写 | 写小文件再 `cat` 一致 |
| **K25** ✅ | Shell 系统类：`mem`/`ps`/`exec` 薄 | `ShellCommandsSystem*` | `exec HELLO.ELF` 与开机路径一致 |

#### 相位 P1b · 汉字 / 字体（K26–K28）· **提前 · 优先**

| PR | 一句话 | 现网对照 | 验收（口述） |
| -- | ------ | -------- | ------------ |
| **K26** ✅ | UTF-8 + 教学 CJK 点阵子集 | I18N1 / `Fonts/cjk*` | 顶栏/桌面标签可显示汉字；ASCII 不回退 |
| **K27** ✅ | TTF 最小光栅（盘上字体→`FontDraw*`） | 现网课路径**未做**；Blocks 加码 | 指定 TTF 渲染若干汉字，观感明显优于点阵 |
| **K28** ✅ | `lang` / UI 字符串表 en\|zh | I18N2 | `lang zh` 后桌面/Shell 标签中文 |

#### 相位 P2 · 窗管 / 桌面（K29–K37）

| PR | 一句话 | 现网对照 | 验收（口述） |
| -- | ------ | -------- | ------------ |
| **K29** ✅ | 单窗：标题栏 + 客户区（Shell 窗） | G 族开窗 | 屏上窗内提示符（可中文） |
| **K30** ✅ | 拖标题 + 焦点 | GuiDrag/Focus | 能挪窗、点选焦点 |
| **K31** ✅ | 关窗 / 重画桌面 | G6 基线 | 关窗不花屏 |
| **K32** ✅ | 双窗 + 简单 Z 序/合成 | GuiCompose | 两窗重叠可分清 |
| **K33** ✅ | 桌面图标 + 双击开 Shell | Desktop/D4 | 双击开/聚焦 Shell 窗 |
| **K34** ✅ | Settings 窗皮 + 改色写回 Theme | SettingsUi/D2 | 改色立即或重启可见 |
| **K35** ✅ | Files 窗：列目录 / 打开 | FilesUi/FB1 | 窗内 `ls`；点 ELF 可 exec |
| **K36** ✅ | 开始菜单 / 任务栏最小 | G13 | 钮可开 Shell/Settings/Files |
| **K37** ✅ | `THEME.CFG` / 分辨率偏好（Boot 可读） | D7 | 改 mode 文档化；热切可后置 |

#### 相位 P3 · 网络对标（K38–K43）

| PR | 一句话 | 现网对照 | 验收（口述） |
| -- | ------ | -------- | ------------ |
| **K38** ✅ | ICMP `ping 10.0.2.2`（builtin） | Net/N 族 | ping 通；串口/Shell 可见 |
| **K39** ✅ | UDP 收发 + Shell 命令 | Udp | `udpsend`/`udplisten` 一类 |
| **K40** ✅ | TCP 最小（单连接对照） | Tcp legacy | listen/connect 一侧可演示 |
| **K41** ✅ | 嵌入 lwIP 最小 + `lwip on` | N-lwip | 默认课路径可切 lwIP |
| **K42** ✅ | DNS / 基础 `Configuration` | 现网网配置 | `dns` 或等价日志 |
| **K43** ✅ | 用户态 `NETDEMO` 能跑 | NetDemo / socket | `exec NETDEMO.ELF` 见 `ok` |


#### 相位 P4 · 存储加厚 / Store / DB（K44–K48）

| PR | 一句话 | 现网对照 | 验收（口述） |
| -- | ------ | -------- | ------------ |
| **K44** ✅ | GPT + 多卷前缀薄（`BLOCKS:`/`ESP:`） | FS2 | `vols`/`ls` 多卷前缀 |
| **K45** ✅ | Files：删/建/改名 | FB2 | 窗或 Shell 均可 |
| **K46** ✅ | `BLOCKS.DB` KV 最小 | DB1 | `dbget`/`dbset` |
| **K47** ✅ | Store 读清单 + 装一包 | Store/S-job 薄 | 桌面或 Shell 装包成功 |
| **K48** ✅ | StoreUi / 作业互斥薄 | PR-S-job | 不与 Shell 装包踩踏 |

#### 相位 P5 · 用户态 / 输入 / 积木面（K49–K54）

| PR | 一句话 | 现网对照 | 验收（口述） |
| -- | ------ | -------- | ------------ |
| **K49** ✅ | Ring3：独立页表 + `execve` 形 | P 族 | 用户 ELF 与内核堆隔离（最小） |
| **K50** ✅ | `fork`/`wait` 或 COW 最小 | FORK.ELF | 课表演示可跑 |
| **K51** ✅ | CRT / `CAT`/`WRITE` 用户程序 | CRT1/2 | 用户态读盘写盘 |
| **K52** ✅ | USB HID 键鼠（并或替 PS/2） | xHCI HID | GTK/真机键鼠走 HID（JX 前对范围） |
| **K53** ✅ | `MEMORY_OPS` / `SCHEDULER_OPS` 钉落 | Modules 积木 | 可切换默认实现不破开机 |
| **K54** ✅ | smoke 对标：无头脚本 ≈ `smoke-boot` | ToyImage 冒烟 | 一键串口断言桌面/Shell/汉字关键字 |

### K46–K54 已收官（摘要）

### K46 已收官（摘要）

`BLOCKS.DB` + `dbget`/`dbset`；细则与手测见文末 [【归档】](#sec-gd) · K46。

### K48 已收官（摘要）

`StoreJob` + `StoreUi` 窗 + Shell/窗互斥；Z 序含 Store。细则见文末归档（TG 后迁）。

### K49 已收官（摘要）

`VirtualMemorySpace` + ELF 私有页 + `ProcessExecPath` 切/还 CR3；`hello` 串口见 `Hello from HELLO.ELF`。

### K50 已收官（摘要）

`ProcessFork` + `SpaceClone` + `FORK.ELF`：串口 `C`/`P`/`done`；fork 恢复点与 `HalSyscallRun` 返回点分离。

### K51 已收官（摘要）

`SYS_OPEN` + 文件 fd；`Crt0`/`CAT.ELF`/`WRITE.ELF`；`NOTE.TXT` + `BLOCKS.ID` 串口可见。

### K52 已收官（摘要）

`Drivers/UsbHid/` 薄栈；`qemu-xhci`+kbd/tablet；Gui/Console 优先 HID；串口 `UsbHid: kbd/tablet ok`。

### K53 已收官（摘要）

`MemoryOps`/`SchedulerOps` 注册面；默认 `MemoryBitmapOps`/`SchedulerRoundRobinOps`；`PhysicalMemory` 拆位图池（≤300）；串口 `MemoryOps: bitmap ok` / `SchedulerOps: round-robin ok`。

### K54 已收官（摘要）

`Runtime/smoke-boot.sh`：无头断言 `Blocks ready` / `Gui: desktop ok` / `Shell` / `Font: cjk32 disk ok`；失败非零。**仅冒烟**；**不是** ≥现网 / B 能力收官。


### K0–K4 细则（已收官）

### PR-K0 细则（已收官）

### 3.1 做

- `KernelMain`：`BootInfoSet` →（X64）`EarlyIdentity` → `AttachEarly`（再 Init 串口；`HalVideoSet` 只存配置）→ `KernelModulesRun`（仅 Serial）→ 串口报完成 → park。  
- `MODULE` / `ModulesRun` / `InitializeSerial` 薄壳。  
- `HalHasFrameBuffer` / `HalConsoleOnly` / `HalPlatformIsVirtSerialConsole` 最小实现（读 `BootInfo`）。  
- `HalVideoSet` / `GetSize` / `FrameBuffer*` 薄存储（无画像素）。

### 3.2 不做

- 真画屏、Font、Theme、GOP 镜像  
- PMM / VMM / 调度 / FS / USB  
- Runtime / QEMU 脚本

### 3.3 验收清单

- [x] `./build.sh x64|arm64|riscv`  
- [x] 串口可见 `KernelMain:` / `[Mod] Serial` / `modules done (K0)`（本机编通；QEMU 随 Runtime）  
- [x] 文档 ★ 指向下一刀 **K1**

### 3.4 本刀落点（已入库）

| 文件 | 作用 |
| ---- | ---- |
| `Core/KernelMain.c` | AttachEarly + RunFull + park |
| `Core/Module.c` `Include/Core/Module.h` | `ModulesRun` |
| `Core/KernelModules.c` | Full 表仅 Serial |
| `Hal/Common/HalCapability.c` | FB / ConsoleOnly / VirtSerial / CpuPark |
| `Hal/Common/HalVideoStub.c` | `HalVideoSet` 等薄存储 |

---

### PR-K1 细则（已收官）

### 做

- X64：`Hal/X64/HalVideo.c`（或拆极薄两文件）实现  
  `HalVideoSet` / `GetSize` / `FrameBuffer*` / `DrawPixel` / `FillRect`  
  （线性 FB、假定 32bpp；无后缓冲、无 Present、无字库）  
- `KernelAttachEarly`：有 FB 时串口打 `WxH`，可选右上角画一小色块作自检  
- Arm64/RiscV：继续空操作桩（或仍走 `HalVideoStub`），保证三架构可编  

### 不做（留给 K2+）

- Font / Theme / `DrawString`  
- 后缓冲、脏矩形、GOP 镜像 / `HalSerialGop*`  
- `Hal/Common/RamFrameBuffer` 真实现  
- 模块表挂 `Video`、PMM  

### 验收

- [x] `./build.sh x64|arm64|riscv`（JX 编通）  
- [x] `Runtime/run.sh --headless`：串口见 `FB 1024x768` / `video self-test` / `park`  
- [x] 落点：`Hal/X64/HalVideo.c`；Arm/RiscV 仍 `HalVideoStub`  

### 本刀落点

| 文件 | 作用 |
| ---- | ---- |
| `Hal/X64/HalVideo.c` | Set / GetSize / DrawPixel / FillRect（直写 LFB） |
| `Hal/Common/HalVideoStub.c` | 非 X64：Set + 空 Draw/Fill |
| `Core/KernelMain.c` | 打分辨率 + 右上角自检色块 |

---

### PR-K2 细则（已收官）

### 做

- 内建 8×8 ASCII（`0x20`–`0x7E`）点阵 + `HalVideoDrawCharAt` / `DrawStringAt`
- `KernelAttachEarly`：有 FB 时左上角打 `Blocks K2`（白字）
- 无 Theme、无平滑、无 UTF-8

### 验收

- [x] `./build.sh x64|arm64|riscv`
- [x] 串口：`font self-test (DrawString)` / `modules done; park`
- [x] 屏上左上角可见 `Blocks K2`

### 落点

| 文件 | 作用 |
| ---- | ---- |
| `Include/Hal/FontGlyph8x8.h` | 点阵表（曾在 Include/Core） |
| `Hal/Common/HalFont.c` | 栅格化 → `DrawPixel`（曾 Core/Font.c；后→Font 积木） |
| `Core/KernelMain.c` | Font 自检一行 |

---

### PR-K3 细则（已收官）

### 做

- X64 `HalSerialGop.c`：`SCREEN_LOG=1` 时按 `SCREEN_LOG_*` 上滚到 FB（Y≥80）
- `KernelMain` 主线走 `SLOG_BOOT`；`HalSerialGopEnable` 接在 `HalVideoSet` 后
- `build.sh`：`SCREEN_LOG=0|1`（默认 0）；Arm/RiscV 仍空操作

### 不做

- 完整 Theme / Present / Desktop ring 叠画

### 验收

- [x] `./build.sh x64|arm64|riscv`；`x64 SCREEN_LOG=1`
- [x] 串口仍见 `KernelMain:` / `park`
- [x] GUI：`SCREEN_LOG=1` 时屏上有 boot 白字行

### 落点

| 文件 | 作用 |
| ---- | ---- |
| `Hal/X64/HalSerialGop.c` | 上滚 / Enable / Mute / Mirror |
| `Hal/X64/HalSerial.c` | WriteChannel → TryMirror |
| `Core/KernelMain.c` | BOOT 通道 + GopEnable |
| `build.sh` | `-DSCREEN_LOG=` |

---

### PR-K4 细则（已收官）

### 做

- `PhysicalMemory*`：恒等窗内最大 Free 区 + 位图 first-fit；抠 Handoff 保留段
- 模块表挂 `Memory`；Init 内 alloc/写/free 自检
- **不**抽 `MEMORY_OPS`（默认实现先稳）

### 验收

- [x] `./build.sh x64|arm64|riscv`
- [x] 串口：`[Mod] Memory` / `PMM: self-test ok` / `park`

### 落点

| 文件 | 作用 |
| ---- | ---- |
| `Include/Core/PhysicalMemory.h` | 对外契约 |
| `Core/PhysicalMemory.c` | 单区位图 PMM |
| `Core/KernelModules.c` | Serial → Memory + 自检 |

---


### K5–K43 刀规划 / 已收官

### K5 规划（已收官 ✅ · 曾 ★）

**一句话**：`Driver` 空壳 + `VirtualMemory` 认领 4GiB 恒等分页；禁整目录搬家。

| 项 | 定调 |
| -- | ---- |
| **Driver** | `DeviceInitialize` / `Enumerate` 薄壳；不 Probe |
| **VMM** | 认领 `EarlyIdentity`；确认 FB 在窗内；分页后写一像素 |
| **验收** | `[Mod] Driver` → `[Mod] VirtualMemory` → `VMM: FB pixel after PG ok` → `park` ✅ |

表序：`Serial → Memory → Driver → VirtualMemory`。

### K6 规划（已收官 ✅ · 曾 ★）

**一句话**：模块表挂 `Video`；可选背缓冲 + `Present`；**不**强制全屏 Clear（接 Boot 黑底）。

| 项 | 定调 |
| -- | ---- |
| **做** | `InitializeVideo`；X64：`HalVideoInitializeBackbuffer`（PMM 页）+ `Present` 整屏 blit；画点默认进背缓冲 |
| **不做** | 脏矩形、Theme、缩放、整夹搬现网 Video |
| **Arm/RiscV** | Init 成功桩 |
| **验收** | `[Mod] Video`；`Video: backbuffer on` / `present ok` → `park` ✅ |

表序：`… → VirtualMemory → Video`。

### K7 规划（已收官 ✅ · 曾 ★）

**一句话**：模块表挂 `Cpu`；X64 装最小 GDT + IDT（门可挂）；**不**开定时器中断风暴。

| 项 | 定调 |
| -- | ---- |
| **做** | `InitializeCpu`；X64：`lgdt` 内核码/数据段 + `lidt`（异常/IRQ 门指向安全 stub）；串口报 ready |
| **不做** | LAPIC 定时器、IOAPIC 路由、TSS/用户段、SMP、整夹搬现网 Arch |
| **Arm/RiscV** | Init 成功桩（本刀不装 GIC/SBI 定时器） |
| **验收** | `[Mod] Cpu`；`Cpu: gdt/idt ok` → `park` ✅ |

表序：`… → Video → Cpu`。

### K8 规划（已收官 ✅ · 曾 ★）

**一句话**：模块表挂 `Scheduler` + `Console`；单核协作壳；串口提示符 + 回显。

| 项 | 定调 |
| -- | ---- |
| **做** | `SchedulerInitialize` 壳；`Console` 横幅/`Blocks>`/读行回显；`\\r`/`\\n` 都行结束 |
| **不做** | 抢占定时器、多核、完整 shell 语法、Gui |
| **验收** | `[Mod] Scheduler` → `Blocks ready` → `Blocks>`；输入回显 ✅ |

表序：`… → Cpu → Scheduler → Console`（中间 USB/FS/… 后挂）。

### K9 规划（已收官 ✅ · 曾 ★）

**一句话**：模块表挂 `FileSystem`；认系统卷标记 `BLOCKS.ID`（本刀吃 Boot handoff）。

| 项 | 定调 |
| -- | ---- |
| **做** | Boot 扫卷写 `OsIdSeen`；Handoff→`BOOT_INFO`；`FileSystemInitialize` 串口报 ready |
| **不做** | 内核侧 AHCI/virtio-blk、完整 FAT API、写文件、Store |
| **Arm/RiscV** | 无 UEFI 扫卷则 stub 软成功 |
| **验收** | `[Mod] FileSystem`；`Fs: BLOCKS.ID ready (Boot handoff)` → `Blocks>` ✅ |

表序：`… → Cpu → FileSystem → Scheduler → Console`。

### K10 规划（已收官 ✅ · 曾 ★）

**一句话**：模块表挂 `USB`；认 Boot 交出的 xHCI 基址（窗内可再读 CAP/VER）。

| 项 | 定调 |
| -- | ---- |
| **做** | `UsbInitialize`；`BOOT_INFO.XhciBase`；窗内读 CAPLENGTH/HCIVERSION；窗外只报 handoff |
| **不做** | 完整 XHCI 环/中断风暴、MSC、HID、高址 MMIO Map |
| **Arm/RiscV** | stub 软成功 |
| **验收** | `[Mod] USB`；`Usb: xhci ok …` → 仍回 `Blocks>` ✅ |
| **Runtime** | `run.sh` 加 `-device qemu-xhci`（q35 默认无 xHCI） |

表序：`… → Cpu → USB → FileSystem → Scheduler → Console`。

### K11 规划（已收官 ✅ · 曾 ★）

**一句话**：模块表挂 `Network`；能认出至少一块网卡（PCI 类码或 virtio）。

| 项 | 定调 |
| -- | ---- |
| **做** | `NetworkInitialize`；X64 最小 PCI 配置空间扫 class `0x02`；串口报 nic ok |
| **不做** | 驱动收发包、lwIP、DHCP、Socket API |
| **Arm/RiscV** | stub 软成功 |
| **验收** | `[Mod] Network`；`Net: nic ok …` → 仍回 `Blocks>` ✅ |
| **Runtime** | `virtio-net-pci` + user netdev |

表序：`… → FileSystem → Network → Scheduler → Console`（Gui 后挂）。

### K12 规划（已收官 ✅ · 曾 ★）

**一句话**：模块表挂 `Gui`；有 FB 时画最小桌面壳（底色 + 顶栏 + 标题），无完整 Theme/窗管。

| 项 | 定调 |
| -- | ---- |
| **做** | `GuiInitialize`；FillRect 背景/顶栏；`DrawString` 标题；背缓冲则 Present |
| **不做** | Theme/TTF、窗口管理、开始菜单、鼠标光标合成 |
| **Arm/RiscV** | 无 FB 则 skip 软成功 |
| **验收** | `[Mod] Gui`；`Gui: desktop ok`；GTK 可见顶栏/标题 → 仍回 `Blocks>` ✅ |

表序：`… → Network → Gui → Scheduler → Console`。

### K13 规划（已收官 ✅ · 曾 ★）

**一句话**：VMM 能把 **4GiB 窗外** 的 MMIO 页映成可访问虚址；用 xHCI BAR 验收。

| 项 | 定调 |
| -- | ---- |
| **做** | `VirtualMemoryMapMmio`（2MiB 大页、虚=物）；VMM 预映 `XhciBase`；Usb 读 CAP/VER |
| **不做** | 完整 xHCI 环、通用 PCI BAR 枚举库、IOMMU、UC 属性钉死 |
| **Arm/RiscV** | MapMmio 失败桩 |
| **验收** | `VMM: map mmio ok`；`Usb: xhci ok … cap=…` → `Blocks>` ✅ |

### K14 规划（已收官 ✅ · 曾 ★）

**一句话**：在已映射的 xHCI 上做最小复位，并读出至少一个端口的连接状态（CCS）。

| 项 | 定调 |
| -- | ---- |
| **做** | `HalXhci` 门面；HCRST；读 MaxPorts + PORTSC.CCS；Usb 胶水打日志 |
| **不做** | 设备枚举、HID/MSC、中断环、完整命令环 |
| **Arm/RiscV** | HalXhciStub |
| **验收** | `Usb: reset ok`；`Usb: port N CCS=`；无设备 CCS=0 也算过 → `Blocks>` ✅ |

### K15 规划（已收官 ✅ · 曾 ★）

**一句话**：QEMU 下用 **PS/2** 键盘把按键送进 `Blocks>`（USB HID 枚举另刀）。

| 项 | 定调 |
| -- | ---- |
| **做** | `HalPs2Keyboard` 门面（X64 i8042）；Console 读行同时 poll 串口与 PS/2；ASCII 子集 |
| **不做** | USB HID 枚举、中断驱动键鼠、完整 keymap / 多布局 |
| **Arm/RiscV** | stub；仍只靠串口 |
| **验收** | 焦点在 GTK 窗时按键有回显；无 PS/2 不卡；串口输入仍可用 ✅ |

### K16 规划（已收官 ✅ · 曾 ★）

**一句话**：内核自己从块设备读 FAT，找到根目录 `BLOCKS.ID`（不再只靠 Boot handoff）。

| 项 | 定调 |
| -- | ---- |
| **做** | `HalBlock`（QEMU：`virtio-blk` 读扇区）；薄 FAT 探根目录；见 `BLOCKS.ID` 打 `Fs: … (kernel)` |
| **不做** | 完整 VFS、写文件、多卷、Store；AHCI 可后刀 |
| **Arm/RiscV** | stub / 仍 handoff |
| **验收** | `Fs: BLOCKS.ID ready (kernel)` → `Blocks>` ✅ |
| **Runtime** | Root=`virtio-blk-pci,disable-modern=on`（vvfat）；ESP 仍 IDE |

依赖：PMM（DMA 缓冲）；legacy IO BAR（勿 modern-only）。

### K17 规划（已收官 ✅ · 曾 ★）

**一句话**：Gui 画出鼠标光标，并能对桌面/顶栏点击给出反馈（串口或屏）。

| 项 | 定调 |
| -- | ---- |
| **做** | `HalPs2Mouse`（i8042 AUX）；XOR 箭头光标；顶栏左键 `Gui: bar click` |
| **不做** | 完整窗管、拖拽改大小、USB HID 鼠枚举（可后刀） |
| **Arm/RiscV** | stub / 无指针则 skip |
| **验收** | `Gui: mouse ok` / `cursor on`；GTK 移鼠见光标；点顶栏有日志；`Blocks>` 仍活 ✅ |

### K18 规划（已收官 ✅ · 曾 ★）

**一句话**：Scheduler 有节拍（LAPIC 定时器），协作/轻抢占，Console 仍可交互。

| 项 | 定调 |
| -- | ---- |
| **做** | `HalLapicTimer`（向量 0x30、掩 LINT）；`Yield`=`sti;nop;hlt`；`Sched: tick` 经 WriteShell |
| **不做** | 完整多任务/优先级队列、SMP、IOAPIC 全路由 |
| **Arm/RiscV** | HalTimerStub / 仍纯协作 |
| **验收** | `Scheduler: timer ok`；`Blocks>` 后周期性 `Sched: tick` ✅ |

### K19 规划（已收官 ✅ · 曾 ★）

**一句话**：从 RootFs 加载并跑一个用户态 `HELLO.ELF`，退出后回 `Blocks>`。

| 项 | 定调 |
| -- | ---- |
| **做** | FatFile 读 `HELLO.ELF`；ElfLoader；`int 0x80` write/exit（本刀 ring0 call 进入；真 ring3 后刀） |
| **不做** | 完整 libc、多进程、动态链接、ring3 |
| **Arm/RiscV** | stub / skip |
| **验收** | `Hello from HELLO.ELF` → `User: exit` → `Blocks>` ✅ |

### K20 规划（已收官 ✅ · 曾 ★）

**一句话**：virtio-net 能发出/收到至少一帧（如 ARP 或 echo 一侧），串口打成功日志。

| 项 | 定调 |
| -- | ---- |
| **做** | legacy virtio-net（与 blk 同类 IO BAR）；最小 TX；可选 RX 一轮；`Net: …` 日志 |
| **不做** | lwIP、Socket、DHCP 全流程、多队列 |
| **Arm/RiscV** | stub |
| **验收** | 一次 TX（或 TX+RX）成功日志；`Blocks>` 仍活 |
| **Runtime** | 已有 `virtio-net-pci` + user netdev；必要时 `disable-modern=on` |

> **驱动刀**：JX 前先对范围（见文首协作约定 #4）。

### K21 规划（已收官 ✅ · 曾 ★）

**一句话**：Font 积木化 + Theme 色板——对标现网 D 族入口，不一次搬齐 TTF/THEME.CFG。

| 项 | 定调 |
| -- | ---- |
| **做** | 画字落点可指认为 Font 积木；Theme 色板；Gui 改用色板；契约后收为 `FontDrawString*` |
| **不做** | TTF、完整 Settings、THEME.CFG 写盘（留给 P2） |
| **Arm/RiscV** | 同编；无 FB 色板空转 |
| **验收** | 顶栏/桌面色来自 Theme；三架构可编；现有光标/shell 不回退 |

> JX 前若收窄为「只 Theme」或「只拆 Font」，改本表再动刀。

### K22 规划（已收官 ✅ · 曾 ★）

**一句话**：可扩展命令表；内置 `help` / `clear` / `echo`；未知命令有提示。

| 项 | 定调 |
| -- | ---- |
| **做** | 命令名→处理函数表；`help` 列命令；`clear` 清串口观感（换行/提示）；`echo` 回显参数；保留 `hello` |
| **不做** | 管道、重定向、完整 ShellCommands* 全家桶 |
| **验收** | `help` 可见；`echo hi`；未知命令提示；`Blocks>` 仍活 |

### K23 规划（已收官 ✅ · 曾 ★）

**一句话**：根目录 `ls` / `cat` 小文本（对标现网 FS 只读入口）。

| 项 | 定调 |
| -- | ---- |
| **做** | Fat 列根目录；按名读小文件到串口；Shell 命令 `ls`/`cat` |
| **不做** | 写盘、多卷、LFN 全套、VFS |
| **验收** | `ls` 见 `BLOCKS.ID`/`HELLO.ELF`；`cat` 可读文本；`Blocks>` 仍活 |
| **落地** | `FatVolume`/`FatDirectory`/`FatFile`；Shell `ls`/`cat`；PTY 冒烟见 `BLOCKS.ID`/`HELLO.ELF`/`KERNEL.ELF`，`cat BLOCKS.ID` → `Blocks root volume` |

### K24 规划（已收官 ✅ · 曾 ★）

**一句话**：根目录最小写：`write` / `mkdir` / `rm`（对标现网 FS 读写入口）。

| 项 | 定调 |
| -- | ---- |
| **做** | 写小文件；建空目录；删文件/空目录；Shell 命令入口 |
| **不做** | 多卷、LFN 创建、回收站、权限 |
| **验收** | `write` 后 `cat` 一致；`mkdir` 后 `ls` 可见；`rm` 后消失；`Blocks>` 仍活 |
| **落地** | `FatMutation` / `HalBlockWrite`；Shell `write`/`mkdir`/`rm` |

### K25 规划（已收官 ✅ · 曾 ★）

**一句话**：Shell 系统类薄命令：`mem` / `ps` / `exec`。

| 项 | 定调 |
| -- | ---- |
| **做** | `mem` 汇报；`ps` 见最近 exec；`exec` 路径跑根目录 ELF |
| **不做** | 完整进程表、多任务切换 UI |
| **验收** | `exec HELLO.ELF` 与开机 HELLO 路径一致；`Blocks>` 仍活 |
| **落地** | `ShellSystem.c`；`ProcessExecPath` / `ProcessLastExecName` |

### K26 规划（已收官 ✅ · 曾 ★）

**一句话**：UTF-8 + 教学 CJK 16×16 点阵；`FontDraw*` 走码点。

| 项 | 定调 |
| -- | ---- |
| **做** | UTF-8 解码；CJK/ASCII 点阵；顶栏/桌面汉字 |
| **不做** | TTF（→K27）、完整 Unicode |
| **验收** | 顶栏/桌面标签可显示汉字；ASCII 不回退 |
| **落地** | `Utf8` / `Font` CJK 点阵 |

### K27 规划（已收官 ✅ · 曾 ★）

**一句话**：盘上 TTF 最小光栅进 Font 积木（Blocks 加码）。

| 项 | 定调 |
| -- | ---- |
| **做** | RootFs `CJK.TTF`；`FontTtf*` 光栅；桌面可见 TTF 汉字 |
| **不做** | 完整字库热加载、字体设计器 |
| **验收** | 指定汉字走 TTF；无字回退点阵 |
| **落地** | `FontTtfLoad`/`Raster`/`Cache`；`HalFpu` 岛 |

### K28 规划（已收官 ✅ · 曾 ★）

**一句话**：`lang en|zh` + UI 字符串表。

| 项 | 定调 |
| -- | ---- |
| **做** | `Locale` / `LocStr`；`lang` 命令；Gui/Console 走表 |
| **不做** | 翻译平台、运行时换字体文件 UI |
| **验收** | `lang zh` 后桌面/Shell 标签中文 |
| **落地** | `Locale.c`；Shell `lang` |

### K29 规划（已收官 ✅ · 曾 ★）

**一句话**：单窗：标题栏 + 客户区（Shell 窗）。

| 项 | 定调 |
| -- | ---- |
| **做** | 桌面顶栏；Shell 窗框/标题/客户区；窗内欢迎语+提示符 |
| **不做** | 拖动/焦点（→K30）、关窗（→K31）、多窗 |
| **验收** | 屏上窗内提示符（可中文） |
| **落地** | `Gui.c` 窗几何；`ConsolePaintBanner*`；Theme 窗色 |

### K30 规划（已收官 ✅ · 曾 ★）

**一句话**：拖标题 + 焦点；鼠路径可跟手。

| 项 | 定调 |
| -- | ---- |
| **做** | 标题栏拖窗；点窗聚焦/点桌面失焦；拖中客户区字仍在 |
| **鼠** | i8042 统一 demux→队列；光标背缓冲 save-under + 小 Present；拖窗 dirty Present；半包空转重同步 |
| **不做** | 关窗、多窗 Z 序、USB HID 鼠（课路径仍 PS/2；`run.sh` 不挂裸 xhci） |
| **验收** | 能挪窗、点选焦点；纯挪光标跟手；拖窗无大片残影 |
| **落地** | `Gui.c` drag/focus；`HalPs2*` demux；`HalVideoPresentRect`；`ConsolePaintBannerBack` |

### K31 规划（已收官 ✅ · 曾 ★）

**一句话**：关 Shell 窗 + 重画桌面（对标现网 G6 / `CloseWindow` 薄）。

| 项 | 定调 |
| -- | ---- |
| **做** | 标题栏右侧 ×；关窗擦足迹并全桌面 Present；**点顶栏**再开 Shell 窗 |
| **鼠** | Init FF/F4；光标背+前双写（无 Present）；半包按 Aux 空转清 |
| **不做** | 淡入淡出、多窗 Z 序、Closing 与用户态竞态全套、开始菜单 |
| **验收** | 关窗不花屏；桌面/顶栏完整；点顶栏可再开窗见提示符；停鼠再动能跟 |
| **对照** | 现网 `GuiOpenClose.c` / 点 × → `CloseWindow`；Blocks 单窗最小路径 |
| **落地** | `Gui.c`：`InCloseBtn` / `CloseShellWindow` / `OpenShellWindow`；`HalPs2Mouse.c` |

### K32 规划（已收官 ✅ · 曾 ★）

**一句话**：双窗 + 简单 Z 序（对标现网 GuiCompose 薄）。

| 项 | 定调 |
| -- | ---- |
| **做** | Shell + 第二窗（About/说明）；点击抬升；重叠时顶窗完整盖住底窗 |
| **不做** | 真 alpha、淡入淡出、开始菜单、桌面图标、任意多窗池 |
| **验收** | 两窗可重叠；点底窗标题/客户区抬到最上；拖/关仍可用 |
| **对照** | 现网 Raise / Z 序；Blocks 固定 2 槽 |
| **落地** | `Window.c` / `WindowPaint.c` + `Gui.c` 轮询 |

### K33 规划（已收官 ✅ · 曾 ★）

**一句话**：桌面图标 + 双击开/聚焦 Shell（对标现网 Desktop/D4 薄）。

| 项 | 定调 |
| -- | ---- |
| **做** | 桌面至少一枚 Shell 图标；双击打开或聚焦已开 Shell 窗 |
| **不做** | 拖图标排版落盘、开始菜单、任意多图标类型全套 |
| **验收** | 双击图标能开/聚焦命令窗 |
| **对照** | 现网 Desktop 图标；Blocks 最少路径 |
| **落地** | `Gui/Desktop.c`；`WindowCompose` 先画图标 |

### K34 规划（已收官 ✅ · 曾 ★）

**一句话**：Settings 窗 + 点色块改 Theme（立即重绘）。

| 项 | 定调 |
| -- | ---- |
| **做** | Settings 窗皮；桌面/标题栏色板点击写回 Theme 并立刻刷新 |
| **不做** | THEME.CFG 落盘（→K37）、完整分类树、真机多显示器 |
| **验收** | 改色后桌面/窗标题立即变色 |
| **对照** | 现网 SettingsUi/D2 薄 |
| **落地** | `ThemeSet*`；`Gui/Settings.c`；Settings 窗+桌面图标 |

### K35 规划（已收官 ✅ · 曾 ★）

**一句话**：Files 窗列根目录；点 `.ELF` 可 exec。

| 项 | 定调 |
| -- | ---- |
| **做** | Files 窗 + 桌面图标；`FatDirectoryListRoot` 列表；点 ELF → `ProcessExecPath` |
| **不做** | 多级目录浏览、拖放复制、图标缩略图 |
| **验收** | 窗内见根目录名；点 `HELLO.ELF` 能跑 |
| **对照** | 现网 FilesUi/FB1 薄 |
| **落地** | `Gui/Files.c`；第四窗槽 |

### K36 规划（已收官 ✅ · 曾 ★）

**一句话**：底栏开始钮 + 弹出菜单开 Shell/Settings/Files。

| 项 | 定调 |
| -- | ---- |
| **做** | 底栏左侧开始钮（顶栏仍仅 Blocks 标题）；菜单向上弹出三项；点项开/聚焦；点别处收起 |
| **不做** | 二级 Apps、关机项、时钟、钉住任务栏图标全套 |
| **验收** | 开始钮可开三窗 |
| **对照** | 现网 G13 薄 |
| **落地** | `Gui/Start.c`；任务栏重绘 |

### K37 已收官

**一句话**：主题/分辨率偏好可落盘，Boot 侧可读；盘上 CJK 点阵 + 布局描述表。

| 项 | 定调 |
| -- | ---- |
| **做** | `THEME.CFG`：色/`mode`；Settings/`mode` 落盘；Boot `VideoTheme` 读 `mode=`；QEMU VGA edid 跟 CFG（默认 1440×900）；`CJK32.BIN` 盘读 18×18×4bpp；`Layout`+`LAYOUT.CFG` |
| **不做** | 热切分辨率、多显示器、BLOCKS.DB（K46）、完整主题包 |
| **验收** | 改色重启仍在；`mode` 冷起生效；汉字走盘上点阵；布局可 CFG |
| **落地** | `ThemeConfiguration` / `FontCjkDisk` / `Layout` / `Runtime/run.sh` edid |

### K38 已收官

**一句话**：Shell `ping` 打通 QEMU 网关 ICMP echo。

| 项 | 定调 |
| -- | ---- |
| **做** | builtin ICMP echo；默认 `10.0.2.2`；ARP→echo→reply；失败码 arp/tx/timeout |
| **不做** | lwIP、DNS、TCP、多网卡、RTT 计时展示 |
| **验收** | `ping` / `ping 10.0.2.2` → `ping: ok` |
| **落地** | `Network/Ping.c` + `ShellCommand` `ping` |

### K39 已收官

**一句话**：UDP 最小收发 + Shell 命令演示。

| 项 | 定调 |
| -- | ---- |
| **做** | 发/收一帧 UDP；Shell `udplisten`/`udpsend`/`udprecv`；本机 IP 回环 |
| **不做** | TCP、lwIP、DNS |
| **验收** | headless：`udplisten 40000` → `udpsend 10.0.2.15 40000 hello-k39` → `udp: sent` + `udp: recv … hello-k39`；`ping: ok` 仍通 |
| **落地** | `Ip.c` / `Udp.c`；`Ping` 共用 ARP/SendIp；`ShellCommand` 三命令 |

### K40 已收官

**一句话**：TCP 单连接对照（listen 或 connect 一侧可演示）。

| 项 | 定调 |
| -- | ---- |
| **做** | 极简 TCP；Shell `tcplisten`/`tcpconnect`；LISTEN 回显 |
| **不做** | lwIP、多连接、拥塞控制、完备重传 |
| **验收** | `tcplisten 5000` + 宿主机 `nc 127.0.0.1 15000` → `tcp: client connected` + `tcp echo: hello-k40`；`ping: ok` |
| **落地** | `Tcp.c`；`run.sh` `hostfwd=tcp::15000-:5000` |

### K41 已收官

**一句话**：编入 lwIP 最小胶水 + Shell `lwip on` 切换课路径。

| 项 | 定调 |
| -- | ---- |
| **做** | 嵌入 lwIP；`lwip on`/`lwip status`；活跃后 `ping` 走 lwIP |
| **不做** | DNS（K42）、NETLIB/NETDEMO（K43）、自动开机 `lwip on`、热切回 builtin |
| **验收** | `lwip on` → `ping` → `ping … (lwIP) …` / `ping: ok` |
| **落地** | `ThirdParty/lwip`；`Hal/X64/LwIp`（`LwIpNetif`/`LwIpIcmp`）；`Lwip.c`；`LWIP=1` 默认 |

### K42 已收官

**一句话**：DNS 查询 + 基础网配置（对标现网 Configuration 薄）。

| 项 | 定调 |
| -- | ---- |
| **做** | Shell `dns`/`net`；`Configuration`（ip/mask/gw/dns）；`LWIP_DNS` + `dns_gethostbyname` |
| **不做** | NETLIB/NETDEMO（K43）、完整 DHCP UI、自动开机 `lwip on` |
| **验收** | `lwip on` → `net` 见 dns=10.0.2.3 → `dns 10.0.2.2` → `dns: … -> 10.0.2.2`；可选主机名 |
| **落地** | `Configuration.c`；`Lwip` Apply/DnsLookup；`lwipopts` `LWIP_DNS=1` + `dns.c`；Shell `net`/`dns`；SLIRP ARP 种子 |

### K43 已收官

**一句话**：用户态网络演示 ELF 能 `exec` 跑通（对标现网 NETDEMO 薄；NETLIB 后刀）。

| 项 | 定调 |
| -- | ---- |
| **做** | syscall socket/connect/read/write/close + `NETDEMO.ELF`；`LwIpSock` TCP 客户端 |
| **不做** | 完整 libToyNet/`NETLIB`、bind/listen/accept、真 ring3 |
| **验收** | 宿主机 `nc -l -p 8888`；Guest `lwip on` → `exec NETDEMO.ELF` → 串口 `ok` |
| **落地** | `LwIpSock.c`；`HalSyscall` 3–6；`User/X64/NetDemo.S` → `NETDEMO.ELF` |

### K44 已收官

**一句话**：GPT / 多卷前缀薄（对标现网 FS2）。

| 项 | 定调 |
| -- | ---- |
| **做** | `GptFindFatParts` + `Volume`；前缀 `BLOCKS:`/`ESP:`/`A:`；Shell `vols`；双 virtio（Root+Esp） |
| **不做** | Store/DB（K46+）、完整 GPT 编辑器、USB MSC 多盘、`TOYOS:` 路径别名 |
| **验收** | `vols` 见 BLOCKS+ESP；`ls BLOCKS:` / `ls ESP:` |
| **落地** | `Gpt.c`/`Volume.c`；`HalBlock` 多盘；`FatVolumeOpenAt`；`run.sh` 第二 virtio |

### K45 已收官

**一句话**：Files / Shell 删、建、改名（对标现网 FB2 薄）。

| 项 | 定调 |
| -- | ---- |
| **做** | Shell `mv` + `FatRenamePath`；Files 选中 + New（`mkdir NEW`）/Del；已有 `mkdir`/`rm`/`write` |
| **不做** | 跨卷 move、递归 `rm -r`、Store/DB、窗内改名/自定名输入框 |
| **验收** | `write`→`mv`→`ls`→`rm`；Files New/Del 后列表变；点选重画 |
| **落地** | `FatMutation` Rename；Shell `mv`；`Files` New/Del；光标前缓冲+描边 |

### K46 已收官

**一句话**：盘上 KV 最小（对标现网 DB1 薄）。

| 项 | 定调 |
| -- | ---- |
| **做** | 根卷小库文件 + Shell `dbget`/`dbset`（名随实现对齐） |
| **不做** | Store 装包、完整 SQL、多库、Theme 批量写库 |
| **验收** | `dbset`→`dbget`→`cat BLOCKS.DB`；复开仍 loaded |
| **对照** | 现网 DB1（现网文件曾名 `TOYOS.DB`；Blocks 固定 `BLOCKS.DB`） |
| **落地** | `DataBase.c`/`DataBase.h`；`ShellCommandDataBase.c`；挂卷后 `DataBaseInitialize`；save=rm+重建防 vvfat Size 旧 |

#### K46 手测步骤（照做）

1. **编核并同步**（在 `~/Blocks`）  
   ```bash
   cd ~/Blocks/Kernel && ./build.sh x64
   cd ~/Blocks/Runtime && ./run.sh --kill
   ./run.sh
   ```  
   （`run.sh` 默认会 sync Kernel；若刚编过可用 `./run.sh --no-sync` 仅当你已手动拷过 ELF。）

2. **等到串口出现** `Blocks>`（GTK 窗可开着；键入以串口/Shell 焦点为准，或点 Shell 窗再敲）。

3. **写库**（在 `Blocks>` 下逐行输入，每行回车）：  
   ```text
   dbset demo hello-blocks
   ```  
   **期望**：`dbset: ok`

4. **读库**：  
   ```text
   dbget demo
   ```  
   **期望**：单独一行 `hello-blocks`（不是 `not found` / `error`）

5. **看盘文件**：  
   ```text
   cat BLOCKS.DB
   ```  
   **期望**：含 `# BLOCKS.DB` 与一行 `demo=hello-blocks`

6. **列目录能看见文件**：  
   ```text
   ls
   ```  
   **期望**：列表里有 `BLOCKS.DB`（或宿主 `RootFs/X64/blocks.db`，vvfat 大小写可能折叠）

7. **持久化（复开）**：关 QEMU → 再 `./run.sh`（或 `--no-sync`）→ 等到 `Blocks>`  
   - 开机日志宜见：`Db: BLOCKS.DB loaded`（若仍 `empty` 则加载失败）  
   ```text
   dbget demo
   ```  
   **期望**：仍是 `hello-blocks`

8. **失败对照**  
   | 现象 | 常见原因 |
   | ---- | -------- |
   | `unknown: dbget` | 未编进含 `ShellCommandDataBase` 的 Kernel，或未 sync 到 Runtime |
   | `dbset: fail` | 卷只读 / 根目录满 / 写盘失败 |
   | `dbget: not found` 但刚 set 过 | 键名不一致；或未 `dbset: ok` |
   | 复开丢失 | 旧核未 sync；或读路径把「成功返回长度」判成失败（已修） |
| `dbget` 有值但 `cat` 只有 `# BLOCKS.DB` | 曾：就地改文件目录 Size 未更新；已改为 save 时 rm+重建 |

无头自动化（可选，同验收句）：  
```bash
cd ~/Blocks/Runtime && ./run.sh --kill
( sleep 14; printf 'dbset demo hello-blocks\n'; sleep 1; printf 'dbget demo\n'; sleep 2 ) \
  | timeout 45 ./run.sh --headless
```

