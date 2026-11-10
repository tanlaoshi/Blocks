# 自研全树 · 编码规范 Review（1108）

> **目的**：对照现行唯一入口 [`规范/编码规范.md`](../../规范/编码规范.md)，对 Kernel 自研树做一次存量盘点。  
> **纪律**：**只记文档，不改代码**（本文件与扫描脚本结果；无行为/无改名/无拆分）。  
> **统计时点**：2026-11-08。  
> **前轮**：[`近30提交-命名行数.md`](近30提交-命名行数.md)、[`第三次代码审查.md`](第三次代码审查.md)（门槛曾用 ≤300 / 函数 ≤200；**本轮改认现行两档 60·300 / 80·500**）。  
> **排期**：PR 表见本文 [§7](#pr-norm-split)；活索引 [`路线图1110.md`](../../路线图1110.md)（★=`PR-NORM-5`；已收 NORM 见 [`项目/进展/路线图1109.md`](../../项目/进展/路线图1109.md)）。

---

## 0 · 结论先行

1. **规模债仍是主战场。** 剔字体后仍有 **8 个 `.c` >500（必须拆）**、**约 44 个 `.c` 在 301–500（容忍线，职责混杂再拆）**；启发式 **函数 >80 行约 213 处**，Services / XHCI / Iwl / 板级 USB·WiFi 最密。
2. **命名：公开函数层大体 PascalCase，短名债在局部变量/参数。** `mXxx` 存量 **未检出**（`sXxx` 口径已落地）。`Buf`/`Len`/`Idx`/`Dst`/`Src` 等整词短名仍遍布约 **409** 个 `.c`——按规范「能还原成完整单词就必须展开」，属**全树慢性债**，只适合路过改，不适合一次翻库。
3. **文件头「人话 / 从哪读 / 别改」覆盖率极低（约 5%）。** 多数文件有注释块，但未按 §四模板钉三件事。
4. **Kernel 方言：`__attribute__` 仍散落自研路径（约 116 处命中，主要为 `weak` / `aligned` / `packed`），未全部收到 `Include/Abi/Compiler.h`。** `User` libc / lwIP 端口按例外处理。
5. **分层大体干净。** `Common`/`Include`/`User` 未扫到直接 `#include "HAL/..."`；共享层 `BOARD_` 能力旗标未见成灾（`BOARD_W` 等为 Snake 游戏宏，非板 ifdef）。
6. **板债旧主角已明显瘦身。** 相对 [`近30提交-命名行数.md`](近30提交-命名行数.md) 的 1.7k 行 `UsbHostController.c` / `Ethernet.c`，当前 >500 名单已换成 Gui/FS/串口/光标等——板拆有进展，**新超标在 Services 与共享 HAL**。

| 严重度 | 含义 | 本轮量级 |
| --- | --- | ---: |
| ⛔ 硬违规 | `.c`>500 或函数>80（规范：必须拆） | 8 文件 + ~213 函数 |
| 🟡 容忍线 | `.c` 301–500 / 函数 61–80 | ~44 文件 + ~201 函数 |
| 🧹 卫生债 | 短名局部变量、缺人话文件头、`*Private.h` 命名混用、裸 `__attribute__` | 成片；路过改 |
| ℹ️ 例外 | 字体数据、lwIP 端口、User hosted libc、Boot/EDK2、ThirdParty | 不进拆刀 |

---

## 1 · 范围

### 1.1 纳入（自研树）

| 树 | 约计 |
| --- | ---: |
| `Kernel/Core` | 64 `.c` |
| `Kernel/Services` | 231 `.c` + 6 `.h` |
| `Kernel/HAL`（自研；含 Board） | 583 `.c` + 149 `.h` |
| `Kernel/Library` | 33 `.c` |
| `Kernel/Include` | 126 `.h` |
| `Kernel/User` | 71 `.c` + 47 `.h` |
| `Kernel/BringUp` | 11 `.c` + 1 `.h` |
| **合计** | **~1322** 个 `.c`/`.h` |

规范原文：「自有树（`Core/Services/HAL 自研/Library/Include/User`）」；本轮 **加扫 `BringUp`**（与内核交接相关）。

### 1.2 明确不评 / 例外

| 类 | 路径 / 模式 | 原因 |
| --- | --- | --- |
| 第三方 | `Kernel/ThirdParty/**` | 规范例外 |
| 点阵 / 字体数据 | `Library/Fonts/**`、`Services/Look/Fonts/**` | 规范例外；行数无意义 |
| 字栅格编排（另议） | `HAL/Common/Video/Glyph.c`（622） | 近乎字体路径；**记例外**，不与 FS/Gui 拆刀搅 |
| lwIP 端口风格 | `HAL/*/LwIp/**`、`HAL/Common/LwIp/**` | 对齐上游 `u16_t` / `ip4_addr_t` |
| User hosted libc | `User/Library/Libc/**`、`User/Library/Blocks/**` | POSIX/课读外观；见 [`Blocks-Hosted-libc-CodeReview-1105.md`](Blocks-Hosted-libc-CodeReview-1105.md) |
| Boot / EDK2 | `Boot/**` | 跟 EDK2；方言例外 |
| 板目录名 | `milk-v-duo-s` 等 | 上游板名；保留 |
| 硬件/ABI 字段 | `ttbr0_el1`、`e_phoff`、`st_ino`… | 不强改 |
| 正式缩写白名单 | `USB` `HCI` `DMA` `IRQ` `PHY` `AIC`… | `Hci`≠禁令里的 `Hc`（HostController） |

### 1.3 方法与局限

- **行数**：`wc -l`；含空行与注释（与历史审查一致）。
- **函数长度**：花括号深度启发式；可能漏嵌套宏/漏签名跨很多行；**作排队依据，不作唯一罪名**。
- **缩写**：标识符分词后对齐禁表；`TimeoutMs` 里的 `Ms`、蓝牙 `Hci` 等需人工剔除误报——下表「局部短名」已用**整词** `Buf`/`Len`… 降低噪声。
- **魔法数 / 静默吞错 / 注释「为什么」**：全树自动化信噪比差，本轮**不假装扫完**；只在 §6 记抽查口径。

---

## 2 · 规模 · 文件

### 2.1 ⛔ `.c` >500（必须拆；已剔 `Fonts/`）

| 行 | 路径 | 备注 |
| ---: | --- | --- |
| 622 | `HAL/Common/Video/Glyph.c` | ℹ️ 建议当字体例外；若强拆另开字柱 |
| 569 | `Services/Shell/Commands/Wifi.c` | P0 · Shell 命令过胖 |
| 560 | `HAL/Arm64/Hal/HalSerial.c` | P0 · 对照 X64 已拆的 `HalSerial/` |
| 558 | `Services/Gui/Cursor/Cursor.c` | P0 |
| 556 | `Services/FileSystem/MountOs.c` | P0 |
| 552 | `Services/FileSystem/FileSystem.c` | P0 |
| 537 | `HAL/X64/Drivers/Video/Igpu/IgpuHwCursor.c` | P0 |
| 510 | `Services/Gui/Cursor/Shape.c` | P0 |

### 2.2 🟡 `.c` 301–500（容忍线；职责混杂再拆）

共 **44** 个（剔字体）。偏高一端：

| 行 | 路径 |
| ---: | --- |
| 484 | `HAL/X64/Drivers/Msc/UsbMsc.c` |
| 444 | `HAL/X64/Drivers/Bus/XHCI/Msc/HubEnumerate.c` |
| 444 | `HAL/Common/HalVideo.c` |
| 424 | `HAL/RiscV/Board/milk-v-duo-s/Sdhci/SdioCis.c` |
| 404 | `Services/Store/Catalog/Query.c` |
| 400 | `Library/Gpt/Gpt.c` |
| 397 | `Services/Apps/SettingsUi/Paint.c` |
| 395 | `Services/Tasks/Hid.c` |
| 393 | `Services/Shell/Console/Write.c` |
| 393 | `HAL/RiscV/Board/milk-v-duo-s/Sdhci/SdioTransfer.c` |
| 392 | `Services/Gui/Backup/Backup.c` |
| 385 | `HAL/X64/Hal/HalSerial/HalSerialWrite.c` |
| 383 | `Services/Store/Ui/Paint.c` |
| 383 | `Services/FileSystem/HoleTest.c` |
| 379 | `Services/Shell/Console/Console.c` |
| 376 | `Core/Process/Process.c` |
| 364 | `Core/VirtualMemory/VirtualMemory.c` |
| … | （其余 301–356：HalDevices×3、Locale、BootLogo、PageTable、Taskbar、XHCI Port/Hub/Irq、Trap 等） |

完整 44 条以本机 `wc` 为准；上表列「先盯」子集。

### 2.3 头文件

| 行 | 路径 | §二要求 |
| ---: | --- | --- |
| 573 | `HAL/X64/Drivers/Bus/XHCI/Private.h` | >500 须在文件头写「为什么不能拆」——现有头说明了 split 历史与共享职责，**接近合规**；可补一句显式「不拆原因」 |
| — | 无 `.h` >1000 | ✅ |

---

## 3 · 规模 · 函数（启发式）

| 档 | 约计 | 规范 |
| --- | ---: | --- |
| >80 行 | **213** | ⛔ 必须拆 |
| 61–80 行 | **201** | 🟡 可留 |
| 含任一 >60 的文件 | **340** | — |

### 3.1 ⛔ 函数 >80 · 最长样例（前 25）

| 行 | 符号 | 路径 |
| ---: | --- | --- |
| 289 | `EnumerateHidOnPort` | `HAL/RiscV/.../UsbHostController/HidEnumerate.c` |
| 288 | `IwlEapolFinishHandshake` | `HAL/X64/.../Iwl/Mac/Eapol/Hand.c` |
| 279 | `EnumerateHidOnPort` | `HAL/Arm64/.../UsbHostController/HidEnumerate.c` |
| 253 | `HttpGet` | `Services/Store/Net/Http.c` |
| 246 | `FeedHid` | `Services/Tasks/Hid.c` |
| 241 | `ClaimPorts` | `HAL/X64/.../XHCI/Msc/ClaimPorts.c` |
| 233 | `WifiFirmwareLoad` | `HAL/Arm64/.../Wifi/FirmwareLoad.c` |
| 227 | `GuiPollMouse` | `Services/Gui/Pointer/Pointer.c` |
| 223 | `SyscallDispatch` | `Core/Syscall/Syscall.c` |
| 221 | `HandleTaskbarClick` | `Services/Desktop/TaskbarClick.c` |
| 216 | `WorkerTask` | `Services/Tasks/TasksWorker.c` |
| 211 | `StoreJobStep` | `Services/Store/Ui/StoreJobStep.c` |
| 201 | `ConfigureMouseInterrupt` | `HAL/X64/.../XHCI/Hid/HidInterruptMouse.c` |
| 191 | `StorePaintList` | `Services/Store/Ui/Paint.c` |
| 183 | `BoardFbSetup` | `HAL/Arm64/.../Mailbox/Framebuffer.c` |
| 182 | `StartController` | `HAL/X64/.../XHCI/Controller.c` |
| 181 | `DesktopSamplePixel` | `Services/Desktop/Sample.c` |
| 181 | `MouseKeyboardSlotInitialize` | `HAL/X64/.../UsbHid/MouseComposite.c` |
| 180 | `AcpiPowerInitialize` | `HAL/X64/Acpi/Power.c` |
| 177 | `IwlAssocRun` | `HAL/X64/.../Iwl/Mac/Assoc/Assoc.c` |
| 176 | `EventHandleTransfer` | `HAL/X64/.../XHCI/EventTransfer.c` |
| 175 | `CloseWindow` | `Services/Gui/Open/Close.c` |
| 175 | `ResetPortEx` | `HAL/X64/.../XHCI/Port.c` |
| 173 | `FatDirStress` | `Library/Fat/DirectoryStress.c` |
| 171 | `IwlScanCollectOnPkt` | `HAL/X64/.../Iwl/Mac/Scan/CollectPkt.c` |

### 3.2 按模块密度（函数 >80）

| 桶 | 约计 |
| --- | ---: |
| Services（总） | 74 |
| HAL/XHCI | 26 |
| HAL/Iwl | 19 |
| HAL（其余） | 46 |
| HAL/Duo 板 | 14 |
| HAL/Pi 板 | 14 |
| Core | 14 |
| Library | 9 |
| Mix2 / User / BringUp | ≤2 |

---

## 4 · 命名

### 4.1 已改善（相对旧审查）

| 项 | 现状 |
| --- | --- |
| `mXxx` 模块静态 | **0 命中**（声明式扫描）；新代码 `sXxx` 口径已在用 |
| `BoardDwc2*` / `HcInit` 公开残留 | **本轮未检出**（旧板债改名大体收过） |
| 反例 `LocStr` / `MSG_ID` / `StallMs` | **未检出** |
| 文件/目录 PascalCase | 自研主路径基本合规；例外见 §4.4 |

### 4.2 🧹 局部 / 参数整词短名（慢性债）

规范表：「能还原成完整单词就必须展开」。下列为**标识符整词**命中（非 `Buffer` 里的子串），按含该词的 `.c` 文件数：

| 短名 | 应写 | 约计文件数 |
| --- | --- | ---: |
| `Len` | `Length` | 132 |
| `Buf` | `Buffer` | 121 |
| `Idx` | `Index` | 78 |
| `Dst` | `Destination` | 75 |
| `Src` | `Source` | 66 |
| `Addr` | `Address` | 53 |
| `Dev` | `Device` | 52 |
| `Val` | `Value` | 48 |
| `Tmp` | `Temporary` | 26 |
| `Cfg` | `Config` | 24 |
| `Ptr` | `Pointer` | 23 |
| `Ctx` | `Context` | 21 |
| `Cur` | `Current` | 19 |
| `Msg` | `Message` | 16 |
| **并集** | — | **~409** |

**执行建议（文档级）**：不单开「全库改 Len→Length」柱；继续「点到哪个文件，顺手收干净、行为不变」。

### 4.3 函数名层 · 仍可见的缩写 / 动词抢前

| 现状 | 规范方向 | 例 |
| --- | --- | --- |
| `Ctxt` | `Context` | `IwlMacCtxtPrep` / `IwlMacCtxtAssoc` / `IwlMacCtxtSend` |
| `Ptrs` | `Pointers` | `WireRingPtrs`、`InitializeRingPtrs` |
| `Initialize*` 动词在前 | 名词在前，或收进操作表短字段 | `InitializeOne`、`InitializeKeyboardDevice`、`InitializeDescs`、`InitializeDefault` |
| `Init` 未写全 | `Initialize` | `QusbWriteInitSequence`（Mix2 PHY；`Init` 段） |
| `OpenSelected` / `CloseWindow` / `StartDragBackups` 等 | 层前缀 + 名词在前，或 static + 表字段 | Gui / FilesUi 一批；部分已是模块内短名——**改前先对操作表铁律** |

蓝牙 `*Hci*`：**保留**（正式名 Host Controller Interface），不要当成禁令 `Hc`。

### 4.4 文件名 · 非 PascalCase

| 区 | 处理 |
| --- | --- |
| `User/Library/Libc/**`、`User/Library/Blocks/**` | ℹ️ libc/课读风格；不进自研改名刀 |
| `HAL/Common/LwIp/include/**` | ℹ️ 上游 |
| `Library/Fonts/terminus*.c` | ℹ️ 数据 |

自研 HAL/Services/Core **主路径文件名**未发现系统性 `snake_case` 违规。

### 4.5 `*Private.h` 形态

| 形态 | 约计 | 规范 |
| --- | ---: | --- |
| 目录内 `Private.h` | 41 | ✅ 首选 |
| `模块名Private.h` / `XxxPrivate.h` | 24 | ✅ 同目录已有 `Private.h` 时允许；部分是历史「又写一遍目录名」 |

**不本轮强统一**；碰到该目录再收（与 1021 记录一致）。

---

## 5 · 注释（文件头）

| 指标 | 数值 |
| --- | ---: |
| 自研 `.c`（剔字体等扫描集） | ~993 |
| 文首 45 行内有 `/*` | ~984 |
| 含「人话 / 从哪读 / 别改」 | **~50（≈5%）** |
| 文首几乎无注释 | ~24（含 Duo Ethernet 拆后辅助、部分 User Apps/Demos、Ui/Button 等） |

§四要求文件头说清三件事——**存量普遍未达标**。新文件应直接写模板；旧文件路过补，勿单开「全文头」柱。

---

## 6 · 数字 / 错误 / 分层 / 方言

### 6.1 魔法数与静默吞错

本轮**未做全树魔法数枚举**（寄存器字面量与有名宏混杂，误报高）。抽查口径留给拆刀时：

- 超时 / 尺寸 / 协议常量是否有名 + 出处注释；
- 失败路径是否处理或显式 `(void)`。

### 6.2 分层

- `Include` / `User` / `Library` / `Core`：**未扫到** `#include "HAL/..."` 直穿。
- 共享层 `BOARD_` 板 ifdef：**未见**成规模；`User/Apps/Snake.c` 的 `BOARD_W/H` 为游戏板宽高，无关板级。

### 6.3 Kernel 方言（§六）

自研路径仍见手写 `__attribute__`（Abi 垫片外），按种类约：

| 属性 | 约计 | 典型位置 |
| --- | ---: | --- |
| `weak` | 66 | `HAL/Common/I2c/I2cDefault.c`、`WifiNetifPrivate.h`、`Hal.c` Board 弱符号 |
| `aligned(64/4096/16)` | ~39 | Duo USB DMA 缓冲、Mix2 xHCI 环、Pi Mailbox |
| `packed` | 3 | USB setup / Mix2 TRB |

规范：垫片只在 `Include/Abi/Compiler.h` 与 `Atomic.h`。  
**建议**：拆/路过时改为 `ALIGNED(n)` / `PACKED` / `WEAK` 一类垫片宏；**不要**为清属性单开大柱（易碰 ABI/链接）。

`User/Include/Libc/stdarg.h` 的 `__builtin_va_*`：文件已自注为垫片边界——ℹ️ 可接受。

---

## 7 · PR 拆分（待选） <a id="pr-norm-split"></a>

> **状态**：细表在本文；活排队见 [`路线图1110.md`](../../路线图1110.md)。刀做只认该档文首 ★。  
> **纪律**（每刀）：① **纯搬家**，函数体逐字不动；② **本刀不改名**；③ 每搬一个就编一次；④ 对外 API / BootMark / 鼠优先语义不变；⑤ 收口报要不要手测。  
> **验收底线**：目标文件 ≤300（硬线 ≤500）；本刀抽出的函数尽量 ≤60（硬线 ≤80）。三架构能编的编。  
> **改名 / 人话文件头 / `Buf`→`Buffer` / `__attribute__`→垫片**：不进下表；路过收，或另开 `PR-NORM-name-*`（未建）。

### 7.1 总序（推荐）

```text
PR-NORM-0 → 1 → 2 → 3 → 4 → 5 → 6
        → 7 → 8 → 9 → 10 → 11 → 12
        → 13 → 14 → 15 → 16
```

- **0–6**：⛔ `.c`>500（Glyph 除外，另议）。先 FS / Shell / HalSerial / iGPU，**后** Gui Cursor（鼠路径，拆完须手测空拖）。  
- **7–12**：函数巨石与密度桶（可随文件拆带走则合并进同模块刀，勿重复拆）。  
- **13–16**：🟡 301–500 且职责已混杂者。

### 7.2 波 A · P0 文件硬拆（>500）

| PR | 对象（行·约） | 一句话 | 形态示意 | 手测 |
| --- | --- | --- | --- | --- |
| **PR-NORM-0** | `Services/FileSystem/FileSystem.c`（552） | 核心瘦到编排；IO/路径/卷辅助外搬 | `FileSystem/` 下 1 核心 + 辅助 | 公司编；挂载/列目录冒烟 |
| **PR-NORM-1** | `Services/FileSystem/MountOs.c`（556） | MountOs 按阶段拆；勿与 NORM-0 同刀改行为 | 同目录辅助，不重复 `FileSystem` 词 | 公司编；OS 盘挂载冒烟 |
| **PR-NORM-2** | `Services/Shell/Commands/Wifi.c`（569） | 命令表薄包装；子命令实现外搬 | `Commands/Wifi/` 或同级 `Wifi*.c` | 公司编；`wifi`/`wififw` 帮助与一两条只读命令 |
| **PR-NORM-3** | `HAL/Arm64/Hal/HalSerial.c`（560） | **对齐** X64 `HAL/X64/Hal/HalSerial/` 目录形态 | `HalSerial.c` + Write/Gop/… | Arm64 编；Pi 串口 boot mark 不回退 |
| **PR-NORM-4** | `HAL/X64/.../Igpu/IgpuHwCursor.c`（537） | HW cursor 探测/装载/热路径分文件 | `Igpu/` 下辅助，不重复模块名 | X64 编；有 iGPU 则光标形状/移动冒烟 |
| **PR-NORM-5** | `Services/Gui/Cursor/Cursor.c`（558） | 光标状态机与绘制分家；**禁止**改泵鼠/Reveal 契约 | 同目录辅助 | **手测**：露光标后空拖不顿 |
| **PR-NORM-6** | `Services/Gui/Cursor/Shape.c`（510） | 形状/热斑/缓存外搬；接 NORM-5 | 同目录辅助 | 随 NORM-5；换形状冒烟 |

ℹ️ **`HAL/Common/Video/Glyph.c`（622）**：字柱另议，**不**发 `PR-NORM-*`。

### 7.3 波 B · P1 长函数 / 密度（可随模块合并）

| PR | 对象 | 一句话 | 手测 |
| --- | --- | --- | --- |
| **PR-NORM-7** | `Services/Tasks/Hid.c` · `FeedHid`（~246） | 键位翻译 / 修饰 / 投递拆辅助；文件已 395 | 键入 Shell；合成键不回退 |
| **PR-NORM-8** | `Services/Gui/Pointer/Pointer.c` · `GuiPollMouse`（~227） | 文件已 ≤300，**只抽函数**；勿半截关泵鼠 | 露光标前后拖鼠；对表鼠优先铁律 |
| **PR-NORM-9** | `Services/Store/Net/Http.c` · `HttpGet`（~253） | 文件已 ≤300，解析/读体/收尾拆辅助 | Store 下一包或 `HttpGet` 探针 |
| **PR-NORM-10** | Pi/Duo `UsbHostController/HidEnumerate.c` · `EnumerateHidOnPort`（~279/289） | 两板**各一刀或同刀双板镜像搬家**；描述符阶段分文件 | 对应板：键鼠枚举绿 |
| **PR-NORM-11** | XHCI 长函数带（`ClaimPorts` / `StartController` / `ResetPortEx` / `EventHandleTransfer` / `ConfigureMouseInterrupt`…） | **只拆函数/辅助文件**；不改环消费与鼠优先 | X64：键鼠+MSC 冒烟 |
| **PR-NORM-12** | Iwl 长函数带（`IwlEapolFinishHandshake` / `IwlAssocRun` / `IwlScanCollectOnPkt`…） | 接已有 iwl 拆目录；纯搬家 | NUC：关联/EAPOL 不回退（点名才真机） |

### 7.4 波 C · P2 容忍线（职责混杂再拆）

| PR | 对象（行·约） | 一句话 | 手测 |
| --- | --- | --- | --- |
| **PR-NORM-13** | `HAL/Common/HalVideo.c`（444） | 与 Glyph/Blit 边界对齐；混杂再拆 | 三架构编；简单 Present |
| **PR-NORM-14** | `HAL/X64/Drivers/Msc/UsbMsc.c`（484） | BOT/SCSI 阶段分文件 | MSC 挂载读 |
| **PR-NORM-15** | Duo `Sdhci/SdioCis.c`（424）+ `SdioTransfer.c`（393） | CIS / 传输若仍两件事缠在一起再拆；已分文件则只收长函数 | Duo SDIO/WiFi 枚举不回退 |
| **PR-NORM-16** | Store/Gui/Shell 301–400 混杂件（`Store/.../Paint`、`Backup`、`Console`、`Query`…） | **按痛点点名**，不一次翻完；一 PR 一模块 | 对应该 UI/命令冒烟 |

### 7.5 不做 / 另册

| 项 | 处置 |
| --- | --- |
| `Glyph.c` / `Services/Look/Fonts/**` | 字柱另议 |
| User libc / Blocks hosted / lwIP 端口文件名与风格 | 例外；见 libc Review |
| 全库 `Len`→`Length` 翻名 | 禁止单柱；路过 |
| 全库补「人话」文件头 | 禁止单柱；新文件必须写 |
| 全库收 `__attribute__` | 禁止单柱；路过改垫片 |
| `PR-NORM-name-*`（Ctxt/Initialize* 等） | **未建**；与搬家刀分开 |

### 7.6 依赖与冲突

| 约束 | 说明 |
| --- | --- |
| NORM-5/6 与手机/核传 ★ | 勿并行乱改鼠路径；Cursor 刀须空窗 |
| NORM-10 与手机 USB 柱 | Duo/Pi HID 枚举 ≠ Mix2 DWC3；别搅同一 diff |
| NORM-11/12 | 纯搬家默认可公司编；真机仅点名 |
| 与旧 `PR-S-split-cont` / board-debt | 板 1.7k 主角已瘦；本表接 **Services/共享 HAL** 新超标 |

---

## 8 · 与旧审查对照

| 旧点（近 30 提交 / 第三次） | 1108 |
| --- | --- |
| Duo/Pi `UsbHostController.c` ~1.7k | 已拆目录；**不再**出现在 >500 名单 |
| Duo `Ethernet.c` ~1.6k | 已拆为多文件；部分辅助仍缺人话文件头 |
| 门槛「一律 ≤300 / 函数 ≤200」 | 改认 **60·300 目标 / 80·500 硬线** |
| `BoardDwc2*` / `HcInit` | 本轮公开残留未检出 |
| `mXxx` | 本轮未检出 |

---

## 9 · 修订

| 日 | 记 |
| --- | --- |
| 2026-11-09 | 排期迁 [`项目/进展/路线图1109.md`](../../项目/进展/路线图1109.md)；候补推星排队（`PR-NORM-0…16`） |
| 2026-11-10 | 未完 NORM 迁 [`路线图1110.md`](../../路线图1110.md)；1109 收官 |
| 2026-11-08 | §7 拆成 `PR-NORM-0…16`；1108 候补索引。仍只文档、不改代码。 |
| 2026-11-08 | 首版：全树扫描 + 只读归档；不改代码。 |
