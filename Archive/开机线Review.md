# 开机线全程 Review（加电 → Worker 正常）

> 依据：对 `Boot/BootPkg`（13 个 .c）与 `Kernel/BringUp`、`Kernel/HAL/X64`（Hal / Architecture / SmpBoot / Driver / XHCI / PS2 / MSC / Block）、`Kernel/Core`（PMM / VMM / Scheduler / Driver）、`Kernel/Services`（FileSystem / Gui / Look / Tasks / Shell / Database）中**开机路径可达**的部分逐行通读。
> 只读评审，**未改一行代码**；正文所有结论都带 `文件:行号`。
> 范围：从 `UefiMain` 到 `Blocks>` 提示符出现、Worker 到达 ready（用户第一次输入之前）。用户态程序装载、网络协议栈稳态、热插拔稳态不在范围内。
> 标记：⚠️ = 会改外显行为（可能卡死 / 花屏 / 键鼠假死 / 配置丢）；🟡 = 隐患与卫生（当前不一定发作）。
> 与 [`路线图1029.md`](路线图1029.md) 的分工：那份按 PR 拆改动，这份按**执行顺序**说清「谁依赖谁、先后错了会坏什么」。

---

## 0 · 结论先行

1. **开机线真正的单点不在驱动，而在「顺序 + 缓存闩」。** 七处阶段里，能一次性把整机卡死的只有三个：EARLY 阶段无 IDT 时的异常（§2.3-K2-1）、`ArchInit` 自 IPI 判定失败（§3.3-H3-1）、块后端被覆盖后读不到盘（§4.3-D4-1）。其余多数是「能起来但不对」。
2. **`ThemeBootScaleApply` 把 `DbInitialize` 提前到了 FileSystem 之前，而 `DbInitialize` 是「一次性闩」**：DB 加载失败也会把 `gReady` 置 1，之后 Gui 阶段再调直接早退。后果不是缩放不对一项，而是**本次会话所有 BLOCKS.DB 配置（theme/font/scale/wallpaper/lang/alias）全部失效**，且之后任一 `DbSet` 会按空内存表整份重写 `BLOCKS.DB`。这是本次评审**最该先修的一条**（§6.3-G6-1）。
3. **开机路径上还有两处「没有墙钟的超时」**：`ArchInit` 的自 IPI 忙等（1e6 次 `pause`）和 AP 就绪轮询（2e6 次 `pause`）。两者都随宿主/主频变化，失败表现分别是整机 Park 与静默掉核，且**都不会在串口留下可定位的数字**。
4. **两处「按 3GHz 估」的时间常量**（`Tasks.c` 的 iwl 静默窗、`BootLogo.c` 的帧/最短停留）决定真机「多久能 ready」与「Logo 动画多快」，主频不同即等比漂移——这解释了为什么 QEMU 上始终看不出真机的拖沓。
5. **「软成功」是一致的设计选择而不是疏漏**（模块表恒 `return 0`、`GuiInitialize` 全 `(void)` 吞返回值）；代价是字体/主题/DB 缺失时**没有一条 WARN 级别以上的外显证据**，只有 `LogDrv` / `BootDbg`（`BootDbg` 在 release 下被编译成空，`BootPrivate.h:14-21`）。

---

## 1 · UEFI Boot（`Boot/BootPkg`）

### 1.1 做了什么（关键调用链）

| # | 位置 | 动作 |
| --- | --- | --- |
| 1 | `Boot.c:26` | `UEFI_BOOT_CONFIG BootConfig` **声明在 `UefiMain` 栈上**，不走 pool——这是能被内核跨 EBS 安全读取的前提 |
| 2 | `Boot.c:30` | `SystemTable` 先落包（供内核以后取 RuntimeServices） |
| 3 | `Boot.c:32-33` | `BootSerialInitialize` + `BootSerialBanner`：直接编程 0x3F8（`Serial.c` 侧为 16550 轮询写，TX 有 1e6 次超时上限） |
| 4 | `Boot.c:36-38` | `BootFillRsdp`（`FillRsdp.c:21`）→ `BootFillXhci`（`FillXhci.c:124`）→ `BootFillDiskGuid`（`Volume.c:100`）：**只读固件表/PCI，不依赖 GOP 与 Kernel.elf**，找不到就填 0 |
| 5 | `Boot.c:41-44` | `GetVideoInfo`（`Video/Video.c:17`）：选模、去重模式表、填 `VIDEO_SELECTION` |
| 6 | `Boot.c:45-49` | `SetVideoMode`（`Video/Video.c:241`）：按 `Sel` 决定是否 `Gop->SetMode`，回填 `VideoConfig`，`Blt` 铺黑（`Video.c:334-346`） |
| 7 | `Boot.c:50-53` | `BootLoadKernel`（`LoadKernel/LoadKernel.c:8`）→ `ReadKernelFile`（`Read.c:189`）→ `CheckKernelFile`（`Elf.c:48`）→ `LoadKernelFile`（`Elf.c:203`） |
| 8 | `Boot.c:56` | `JumpToKernel`（`JumpToKernel.c:30`）：`GetMemoryMap` → 多留 16 个描述符 → `ExitBootServices`（最多 8 次）→ `Entry(BootConfig)`（`JumpToKernel.c:85`） |
| 9 | `KernelEntry.S:20-21` | `leaq gEarlyStack(%rip),%rsp` + `addq $65536,%rsp` 换栈 → `call KernelHandoff`（`:27`），此后 Boot 侧栈不再被内核使用 |

选模细节（判据要写死，否则回归不可判）：`Video.c:88-106` 优先沿用固件当前模式（仅当**没有** `mode=` 目标且当前 ≥640×480）；否则 `:142-164` 按 `SCORE_CFG_EXACT → ScoreModeNative/ScoreModeQemu` 打分，`:168-174` 取最大；`Sel->BestScore` 只用于诊断（`:225`），「有没有选到」由独立开关 `FoundMode` 判定（`:216-219`）。

### 1.2 顺序约束

- **串口必须第一**（`Boot.c:32`）：后面每一步都可能 `FailVideo` 返回错误码，没有串口就只剩固件那句「boot failed」。
- **`Fill*` 在选模之前**（`Boot.c:35-38`）：三者都不依赖 GOP，注释写明「找不到则填 0 继续」。推后到 `GetVideoInfo` 之后不会坏，但会把「PCI 扫描（`FillXhci`，遍历 256 路总线）」的耗时挪到用户看得见黑屏之后。
- **`GetVideoInfo` 必须在 `SetVideoMode` 之前**：`SetVideoMode` 第一件事就是检查 `Sel` 是否为空（`Video.c:252-254`），失败返回 `EFI_NOT_READY`。顺序反了不是「选错模式」而是**直接不启动**。
- **`SetVideoMode` 在 `LoadKernel` 之前**：`Boot.c:45` 在 `:50` 前。收益是装内核期间屏上已有最终分辨率 + 黑底，`LoadKernel` 的日志（`LoadKernel.c:35`）落在干净的屏上；反过来先装内核再切模，屏上会先出现一段旧模式的文字再被 `Blt` 抹掉。
- **`LoadKernel` 必须在 `ExitBootServices` 之前**：整条链依赖 `gBS->AllocatePool/AllocatePages`（`Read.c:50/82/89`、`Elf.c:248`）。没有第二条路。
- **`BootConfig` 必须是栈/静态**（`Boot.c:16-25`）：EBS 之后 Boot 的 BSS 不再可信，只有栈与「显式交接出去的 pool 页」可用；内核 `KernelHandoff.c:179` 第一件事就是整包复制。改这三处任一处都会让内核拿到悬垂指针。

### 1.3 可疑点

- **B1-1 ⚠️ `JumpToKernel` 的 8 次重试只有一句终点日志**（`JumpToKernel.c:52-92`）。`ExitBootServices` 失败后不区分 `EFI_INVALID_PARAMETER`（MapKey 变了）与 `EFI_UNSUPPORTED`（固件拒绝），也不打印描述符数量/MapKey，循环耗尽后只留 `ExitBootServices Failed After Retries: %r`（`:90`）。**外显症状**：某固件上开机停在 UEFI Shell，屏幕上只有这一行，看不出是「谁在 EBS 之间又分配了内存」。注释 `:12-22` 只解释了「内核返回」这条异常路，没解释这 8 次的收敛依据（8 是字面量，`:52`）。
- **B1-2 ⚠️ 内核手里会拿到一个跨 EBS 的固件函数指针**：`Video.c:330` 把 `Gop` 指针写进 `BootConfig->GopProtocol`，注释（`UefiBootConfig.h` 顶部）把它当作可用的「热切」通道。目前**没有真正调用**：`HalVideoCanHotSetMode` 只在 Bochs VBE 下返回 1（`HalVideo.c:68-80`），`VideoGopSetMode` 自带拒绝逻辑。**外显症状**（一旦有人放开）：`SetMode` 时 `#PF → #UD@0`，正是 `HalVideo.c:70-74` 注释里 NUC 的历史现场。
- **B1-3 🟡 `SetVideoMode` 内部判空不一致**：`:262` 判断了 `Gop->Mode != NULL && Info != NULL`，`:290-294` 又无条件解引用同一对指针；`GetVideoInfo:109` 也是无条件 `Gop->Mode->MaxMode`。**外显症状**：只在「固件有 GOP 但当前无模式」的怪机器上崩，且发生在串口已开、`SetVideoMode` 头一行之后，日志会断在 `Boot: SetVideoMode` 与 `Boot: GOP Cleared` 之间。
- **B1-4 🟡 `GetVideoInfo` 的 `QueryMode` 失败路径漏 `FreePool`**：`:110-113` 在 `EFI_ERROR(Status)` 时 `continue`，但 `ModeInfo` 是固件按 `InfoSize` 分配的（`:110`）。**外显症状**：无（一次开机几十 KB），属「错误被吞」的典型：同一个函数在 `:115`/`:177` 是**正确**释放的，说明写的人知道要释放。
- **B1-5 🟡 `KeepCurrentMode` 只判宽高，不判可用性**：`Video.c:96` 用 `CurW >= 640 && CurH >= 480`，没有走 `IsModeUsable`（`Score.c:19-30`，那里会拒 `PixelBltOnly`）。**外显症状**：固件当前模式是 `PixelBltOnly` 时，Boot 侧清屏走 `Blt` 看不出问题（`Video.c:344`），内核直接往 LFB 写就花屏/全黑——而 QEMU 永远不复现。
- **B1-6 🟡 ELF 的「页对齐预检」是死代码**：`Elf.c:116-117` 先 `Start = PAddress & ~0xFFF`，`:129` 再判 `(Segs[i].Start & 0xFFFULL) != 0` —— 过滤后必然为 0，永不触发。本意（`:31-34` 注释「先自己查，否则只会看到泛化的 AllocatePages Failed」）落空。**外显症状**：链接脚本一旦把 PT_LOAD 的 `PAddress` 改成非页对齐，报错就是 `Elf.c:248` 的 `AllocatePages Failed`，指不回段号。
- **B1-7 🟡 `CopyMem` 用 `PAddress` 而不是 `SegBase`**：`Elf.c:268` `SetMem(SegBase, SegPages*4096, 0)` 与 `:270-271` `CopyMem((void*)Ph->PAddress, ...)` 混用两个基址。当前 `link.ld` 保证段对齐所以等价；一旦不对齐，除 B1-6 的失败外还会**在 Allocate 页之外写**（越界写内核物理内存）。
- **B1-8 🟡 内存区表静默截断**：`KernelHandoff.c:91-93` `AddRegion` 超 `BOOT_MEMORY_REGIONS_MAX`（64，`BootHandoff.h:9`）或 `Size==0` 直接 `return -1`，**全程无日志**。**外显症状**：`PMM: free=` 明显小于物理内存（`PhysicalMemory.c:31-35`），后续大分配（4K 后缓冲 33MB、窗口备份）在别的机器上成功、在这台失败——表现成「桌面某功能随机不可用」。

### 1.4 与注释不符

| 位置 | 注释说 | 代码做 |
| --- | --- | --- |
| `Video/Theme.c:3` | 「`mode=` **只认第一行**」 | 只认**第一条能解析的** `mode=` 行；解析失败（`MODE_LINE_NONE`）继续看下一行（`:82-100`，且 `:89-93` 自己承认「别让一行笔误吃掉整份配置」）。文件头这句是旧结论没跟改。 |
| `Elf.c:31-34` | 「相邻段盖住就自己先查」 | 对齐检查恒假（B1-6），且重叠检查只覆盖「首段末页向上取整后盖住次段起点」一种形态（`:126-143`）。 |
| `BootPrivate.h:26-39` | 分值表里列了 `SCORE_ASPECT_FALLBACK`，注释说「请求的宽高比不在 GOP 里时，跟显示器宽高比」 | 该档只在**同时有 `mode=` 与 EDID** 时才可能出现（`Video.c:151-157`）；`mode=auto`/无目标时走 `ScoreModeNative`，`SCORE_ASPECT_FALLBACK` 永不出现在打分里。 |
| `Serial.c` 写路径 | 「16550 轮询写」 | `SerialPutChar` `Timeout = 1000000` 次 `pause` 后**不报错直接写**（`Serial.c:176-180`）：FIFO 满时字符丢掉且不计数。 |

---

## 2 · Kernel 早期（`Kernel/BringUp`）

### 2.1 做了什么（关键调用链）

```
KernelEntry.S:20-27  →  gEarlyStack  →  KernelHandoff
  KernelHandoff.c:167  HalSerialInitialize
  KernelHandoff.c:179  UefiBootConfig = *BootConfig        （整包复制，PR-15 契约）
  KernelHandoff.c:180-182  FillHandoff / FillHalPlatform / FillHandoffRegions
  KernelHandoff.c:184  EnableEarlyIdentityMap()            （4GiB 恒等，EarlyIdentity.c）
  KernelHandoff.c:185  BootHandoffSet
  KernelHandoff.c:187  KernelMain
    Kernel.c:142  KernelAttachEarlyVideo
        HalSerialInitialize / LibWriteRegister / ElfArchRegister / HalVideoSet
        FontInitialize / ThemeInitialize / HalSerialGopEnable   （仅当有 FB）
    Kernel.c:143  ModulesRun(表)          ← 三张表见 Modules.c:64-98
    Kernel.c:144  KernelAfterModules      ← FontSetById + GopMirror(0) + 可选 Announce
    Kernel.c:145  Finish                  ← 串口壳 或 KernelMainCommon
    Kernel.c:149-156  Create Shell/Gui/Worker(+Input) → SchedulerStart
```

模块表（`Modules.c:43-98`）：

| 阶段 | Full（真机/QEMU 桌面） | Virt（ConsoleOnly） | VirtDesktop |
| --- | --- | --- | --- |
| EARLY | Serial → PhysicalMemory → VirtualMemory | 同 | 同 |
| DISPLAY | Video | —— | Video |
| PLATFORM | Cpu → Driver → Smp | 同 | 同 |
| INPUT | USB | USB | —— |
| SESSION | Scheduler → Console | 同 | 同 |
| 字面量 | FileSystem → Gui → Network | FileSystem → Network | FileSystem → Gui → Network |

### 2.2 顺序约束（有代码依据的硬约束）

1. **PMM 必须紧挨在 VMM 前，不只是「早」**：`HalPageKernelSetup` 的每个页表页都来自 `PageAllocTable()`（`PageTable.c:69/79`）→ `PhysicalMemoryAllocatePage()`（`Walk.c:52-58`）。VMM 前没有 PMM 就是 `PageAllocTable` 返回 0 → `HalPageKernelSetup` 返回 -1 → `VirtualMemoryInitialize` 返回 -1 → 模块失败 → `KernelParkForever`（`Kernel.c:134-138`）。
2. **VMM 必须在 Video 之前**：`VideoInitialize` 里 `HalVideoInitializeBackbuffer` 要 PMM 分页（`HalVideo.c:37-66`），`HalIgpuMmioInitialize` 要 `VirtualMemoryEnabled()`（`IgpuMmio.c:129-132`）。顺序反了，`DisplayHardwareInitialize` 全部静默跳过（`IgpuMmio.c:129-132` 只打一行 `igpu mmio defer`），桌面能起但**没有任何 GPU 加速/Present 通道**。
3. **`KernelAttachEarlyVideo` 必须早于所有会写屏的模块**：注释 `Kernel.c:94-97` 写明「后面模块 `gConsole.Write` 若 `Width=0` 会空转」。它同时把 `LibWrite` 挂上（`Kernel.c:107`），所以 Serial 模块里的 `PMM: free=` 之类才有人接（`PhysicalMemory.c:31-35`）。
4. **Cpu 必须早于 Driver / Smp**：`CpuInitialize` 里 `HalInitialize→ArchInit` 装 GDT/IDT/TSS/LAPIC/IOAPIC（`Architecture.c:182-202`），`HalTimerInitialize`（空实现，`Hal.c:142-143`）、`HalSyscallInitialize`（`:179-188`）。Driver 的 MMIO 绑定、Smp 的 IPI 都依赖 IDT/LAPIC 已就位。
5. **Smp 必须早于 USB**：xHCI 的 MSI 目的核由 `InputIrqCpu()` 选 `HalCpuApicId(2)`（`Irq.c:12-14`、`:196`）。`gApicIds[]` 由 Smp 模块填（`SmpBoot.c:131+`）。顺序反了，`HalCpuApicId(2)` 在 `gCpuCount<=0` 时退回 BSP id（`SmpBoot.c:84-92`），IRQ 落到一个**没人 drain 的核**上。**外显症状**：桌面后键鼠「点一下动一下」或干脆假死。
6. **SESSION 必须早于 FS/Gui/Net**：`SchedulerInitialize` 建 runq/锁（`Scheduler.c:87-98`），`ConsoleInitialize` 登记命令表（`Console.c:366-379`）。`ModulesRun` 甚至在进表前先 `ConsolePublishAll()` 一次（`Modules.c:163-164`），注释写明「Gui 等会早于 Console 模块登记命令」。
7. **INPUT（USB）必须早于 FileSystem**：U 盘起时 MSC 的 claim/mount 在 FS 模块里（`FileSystem.c:353-412`），claim 依赖 xHCI 已 Init。反过来先 FS 则 U 盘系统永远挂不上，只剩 `block: no drives`。
8. **FileSystem 必须早于 Gui**：`GuiInitialize` 顺序是 DB → 字体 → 主题 → Locale（`WindowManager.c:242-256`），其中 `FontLoadAssets` 列 `Assets/Fonts`（`Fonts.c:389-401`）、`FontTtfLoad` 读 `BLOCKS:Assets/Fonts/UMING.TTF`（`TrueTypeLoad.c:205-214`）、`ThemeLoad` 读 `THEME.CFG`（`Config.c:92-105`）。顺序反了 → 无 TTF、无主题、无 CJK，桌面全豆腐块。

### 2.3 可疑点

- **K2-1 ⚠️ EARLY 阶段（含 VMM 打开分页）没有 IDT**：`ArchIdtLoad()` 在 `ArchInit` 里（`Architecture.c:188`），而 `ArchInit` 属于**第 5 步的 Cpu 模块**；VMM 是第 3 步（`Modules.c:43-46`），`VirtualMemoryEnable()`（`VirtualMemory.c:327-330`）在**没有异常处理器的状态下换 CR3**。**外显症状**：映射写错/越界访问 = 三重故障 = **静默重启或黑屏死机，串口最后一行停在 `vmm: enable` 之前**，没有任何 `#PF CR2`。这是整条开机线上唯一「连日志都没有」的失败面。
- **K2-2 ⚠️ 模块失败的唯一处理是 Park，且日志不带原因**：`ModulesRun:169-172` 打 `***** Module X Failed *****` 后返回 -1，`KernelRunOrPark`（`Kernel.c:134-138`）直接 `HalCpuPark` 死循环。**外显症状**：屏上 Logo 停在某个百分比（`ModLogoPct`，`Modules.c:135-152`），串口停在 `***** Module Gui Failed *****`；**看不到返回值**（`Init()` 的 `int` 被丢掉，`:169`）。
- **K2-3 🟡 `ModLogoPct` 按名字首字母猜阶段**（`Modules.c:135-152`，`C`+`p`/`D`+`r`/`S`+`m`/`U`/`F`）：`Scheduler` 与 `Sm`p 不冲突是巧合；`Console` 与 `Cpu` 也不冲突靠的是比较两个字符。**外显症状**：新增一个 `S` 开头的模块（如 `Sound`）会让 Logo 卡在某档或跳档——不影响功能，但会让人以为卡住。
- **K2-4 🟡 双份 `DisplayHardwareInitialize`**：`Video.c:45` 与 `Driver.c:203` 各调一次，五连调用（`HalIgpuMmioInitialize/Forcewake/Gtt/Blit/PresentPrepare`，`Initialize/Video.c:14-23`）**没有 once 闩**。只有 `IgpuMmioInit` 自带幂等（`IgpuMmio.c:123-125`），其余四个的幂等性靠实现自觉。**外显症状**：Logo 已上屏后（DISPLAY 阶段）再被 Driver 阶段重跑一次 forcewake/GTT，偶发**闪黑一帧或撕裂**；串口会看到两遍 `igpu` 日志。
- **K2-5 🟡 `ModulesRun` 里的 `HalInputPoll` 门闩是「非虚拟机」**（`Modules.c:178-180`）：真机每模块后盲 drain 一次，QEMU 不做。注释 `:174-177` 的理由（防事件环塞满）只在真机成立——代价是每个模块后多一次 USB 轮询，且**这段 drain 与 `UsbInitialize` 自己的 `DrainHid`（`Usb.c:29-41`，8 次 Poll + 清空队列）重复**。属可接受的防御，但两处代码各写一遍门闩，改一处会漂。
- **K2-6 🟡 `KernelAppendDecimal4` 只支持 4 位十进制**（`Kernel.c:71-76`）：`Boot: FB 3840x2160` 没问题，但 `:86-88` 对 `HorizontalResolution` 不做范围检查。**外显症状**：8K 屏（7680）会打印成 `7680`（正好 4 位，侥幸），5 位分辨率会打出错误数字。纯日志问题。

### 2.4 与注释不符

| 位置 | 注释说 | 代码做 |
| --- | --- | --- |
| `Modules.c:35` | 「EARLY：Serial → PMM → VMM，早日志；**PMM 必须紧挨在 VMM 前**」 | 成立，且原因比注释更硬（VMM 的页表页来自 PMM，见 §2.2-1）；但**注释没写这条硬依赖**，只写了「紧挨」，容易被当成排版要求。 |
| `Kernel.c:43-44` | 「Count≥HAL_INPUT_CPU_MIN 才 Create Input@CPU2。Start 再钉核。**失败只打日志**」 | `KernelTrySpawnInput` 失败确实只打日志（`Kernel.c:50-52`），但 `KernelSpawnOrPark`（Shell/Gui/Worker）失败是 **Park**（`:35-40`）。同一文件两种失败策略，注释只交代了前者。 |
| `Initialize/Cpu.c:4` | 「HAL / 定时器 / syscall」 | `HalTimerInitialize()` 在 X64 是**空函数**（`Hal.c:142-143`），`HalTimerSetInterval` 也是空的（`:145-147`）。真正的定时器在 `SchedulerStart` 里 `HalTimerStart`（`Start.c:236`）才起。 |
| `Initialize/Video.c:4` | 「帧缓冲、字体、主题和核显探针」 | 还多做了一件影响全局的事：`FontInitialize` + `ThemeInitialize` 各是**第二次**调用（第一次在 `KernelAttachEarlyVideo`，`Kernel.c:111-112`），`ThemeInitialize` 会把 `gThemeUiScale` 重置成 100（`Theme.c:31`）。 |

---

## 3 · HAL 层（`Kernel/HAL/X64`）

### 3.1 做了什么（`ArchInit` 一条线）

| 步骤 | 位置 | 动作 |
| --- | --- | --- |
| 1 | `Architecture.c:183` | `HalFpuEnableThisCpu`（CR0/CR4 的 SSE 位） |
| 2 | `:184` `:122-161` | `GdtLoad`：5 个基础段 + 每核 2 个 TSS 描述符（`gGdt[5 + HAL_MAX_CPUS*2]`，`:19`），far return 重载 CS，重载 DS/ES/SS |
| 3 | `:185` `:173-179` | `ArchTssInstall`：BSP 的 TSS.RSP0 指向 `gKernelIstStack[0]` 顶（16KiB，`:40`），`ltr` |
| 4 | `:186-187` | `ArchPicMaskAll` → `ArchLapicEnable` |
| 5 | `:188` | `ArchIdtLoad`（**这是 IDT 第一次可用**） |
| 6 | `:189` | `IoApicInit`（`IoApic.c:72-110`）：解析 MADT 拿基址，**先把全部 RTE 掩掉**（`:105-107`） |
| 7 | `:190-197` | `sti` → `ArchLapicSelfIpi(VEC_XHCI)` → 1e6 次 `pause` 等 `gArchIrqCount` → `cli` |
| 8 | `:201` | `gArchIrqCount > 0 ? 0 : -1` ← **返回值直接决定整机是否继续** |
| 9 | `:208-248` | `ArchApInit`（每 AP）：切共享 GDT、`ltr` 本核 TSS、`lidt`、开 LAPIC、`ArchSyscallMsrInit`、`HalPatApplyWc`、`HalFpuEnableThisCpu` |

定时器：`TimerStart`（`Dispatch.c:159-165`）写 `LAPIC_TIMER_DIV=0xB`（divide-by-1）与 `INIT=50000`；`HalTicksPerSec`（`Hal.c:157-173`）**硬编码** QEMU 20000、真机 250（注释 `:157-160` 承认「墙钟拍长随 APIC 总线变」）。SMP：`HalSmpStartApplicationProcessors`（`SmpBoot.c:131+`）按 MADT 拉 AP，`SmpApEntry`（`:41-77`）先 `TimerStart` 跑 5e6 的慢周期，`SchedulerIsOnline` 后改回 50000（`:70`）。

### 3.2 顺序约束

- **IDT 必须早于任何可能异常的代码**：`ArchIdtLoad` 在 `:188`，而 `IoApicInit`（`:189`）已开始 MMIO 访问。两者之间只有一条指令的距离；这也是为什么 §2.3-K2-1 那么致命——EARLY 阶段根本没有这条线。
- **TSS 必须早于 IDT**：`int 0x80`/异常从用户态或 `iret` 回来要 `RSP0`（`Architecture.c:163-171`）。`ArchTssInstall`（`:185`）在前，`ArchIdtLoad`（`:188`）在后，顺序正确。
- **`gsti` 必须在 LAPIC 与 IOAPIC 都就绪之后**（`:187` → `:190`）：先开中断再 `LapicEnable` 会收到未映射向量；先 `sti` 再掩 RTE 会收到中断风暴。
- **`HalSyscallInitialize` 必须在 `ArchTssInstall` 之后**：`HalSyscallInitialize` 装 `0x80` gate（`Hal.c:179-188`），`ArchSyscallMsrInit` 读 `gTss[0].RSP0` 写进 `ARCH_CPU_LOCAL.KernelRsp`（`Architecture.c:89`）。若 `RSP0` 还是 0，SYSCALL 一进来就用 `gs:[0]`=0 当栈。
- **AP 的 `TimerStart` 在 `SchedulerIsOnline` 之前**（`SmpBoot.c:51` vs `:62`）：靠 `SchedulerOnTimer` 的 `if (!gSchedulerOnline) return 0;`（`Signal.c:144-146`）兜住，不是靠顺序。这是一个**隐式依赖**：删掉那个 gate 就立刻在未初始化调度器上切任务。

### 3.3 可疑点

- **H3-1 ⚠️ 自 IPI 是开机必经关卡，没有重试、没有墙钟**：`Architecture.c:190-201`。1e6 次 `pause` 在 3GHz 上约 10~30ms，在 TCG/弱虚拟化下可能不够；一旦 `gArchIrqCount==0` 就 `return -1` → `Cpu.c:11-13` → 模块 Fail → `KernelParkForever`。**外显症状**：串口停在 `***** Module Cpu Failed *****`（`Modules.c:170`），屏上 Logo 停在 20%（`Modules.c:136-138`），**除此之外没有任何信息**——`DebugWrite` 那行 `IDT ready, LAPIC on, self-IPI ok=0`（`:198-200`）在 `DebugWrite` 未上串口的构建里看不到（`Debug.c` 侧决定，Studio/课堂构建常见）。这是我最想加「重试 N 次 + 打印 `gArchIrqCount` 到 BootLog」的地方。
- **H3-2 ⚠️ AP 就绪轮询不是墙钟**：`SMP_READY_POLL_MAX = 2000000u`（`SmpBoot/Start.c:30`）、`TrampSize` 校验、两次 SIPI（`:136-148`）。次数 × 每次 `pause` 的耗时随主频变化，慢机器上**误判超时** → `ParkAp`（`:149-155`）→ `gCpuCount` 减少。**外显症状**：串口 `Smp: AP Timeout APIC=NN`；整机继续跑，但 `HalCpuCount() < HAL_INPUT_CPU_MIN`（4，`Hal.h:155`）→ **`KernelTrySpawnInput` 不建 Input 任务**（`Kernel.c:47-49`）→ 稳态只能靠 `YieldForPollInput` 里每圈一次的 `HalInputPoll`（`Tasks.c:66-75`）→ 鼠标/键盘迟滞，`GuiTask` 的 `GuiPollMouse` 也不再每圈跑满。这条比 H3-1 隐蔽得多。
- **H3-3 🟡 `HalTicksPerSec` 是常量不是测量值**：`Hal.c:161-172`。所有「秒」级行为（`Sleep`、`Clock` 倒数、MSC 热插拔 5s 节流 `UsbMsc.c:330/422`、失根大窗）都按 250 或 20000 换算。**外显症状**：NUC 上 `sleep 1` 偏快/偏慢、设置页倒数与真实秒不符（`Clock.c:119` 的注释已经承认「真机 250 常偏慢→倒数翻倍快，勿用 LAPIC 计秒」）。
- **H3-4 🟡 `HalCpuReboot` 里写死 Structure 偏移**：`Hal.c:60-63` 用 `*(void**)((UINT8*)St + 88)` 取 RuntimeServices、`+104` 取 `ResetSystem`。`UefiBootConfig.h` 里有 `_Static_assert` 保证自己那份结构偏移，但**没有一处断言 EFI_SYSTEM_TABLE 的布局**。**外显症状**：重启走到最后一档时 `#GP`/挂死（前五档都没生效的机器上才可见）。
- **H3-5 🟡 `HAL_MAX_CPUS` 之外的 AP 被夹到最后一核**：`SmpBoot.c:46-48`、`Architecture.c:211-213`（`LogicalCpu = HAL_MAX_CPUS - 1`）。两台 AP 抢同一个 TSS/RSP0/`gApStacks[]` 槽。**外显症状**：`-smp 64` 之类的极端 QEMU 上随机 `#DF`；真机不受影响（核心数 << `HAL_MAX_CPUS`）。

### 3.4 与注释不符

| 位置 | 注释说 | 代码做 |
| --- | --- | --- |
| `Hal.c:157-160` | 「QEMU≈20000 拍/s；NUC 实测约 250 拍/s」 | 注释自己写了「实测」，代码是**写死分支**（`:167-172`），没有测量也没有校准入口。 |
| `SmpBoot.c:52` | 「AP 验证用慢定时器，避免 QEMU 下高频 IRQ 拖死启动路径」 | 慢周期 `5000000`（`:53`）在 `SchedulerIsOnline` 后改回 `50000`（`:70`）；中间的窗口**取决于 BSP 走完模块表的时间**，注释没说这段会有多长。 |
| `SmpBoot.c:71` | 「首入 idle 前关 IF；细节见 `SchedulerApStart` 注释（防 OnTimer 砸 Frame）」 | 真正的 gate 是 `SchedulerOnTimer` 的 `!gSchedulerOnline`（`Signal.c:144`）**加上**这行 `cli`；两处任一处被删都会炸，注释只提了前者。 |

---

## 4 · 驱动枚举（PCIe / NVMe / xHCI / HDA / PS/2）

### 4.1 做了什么（绑定顺序）

| 顺序 | 位置 | 动作 |
| --- | --- | --- |
| 1 | `Driver.c:198` | `DeviceInitialize` |
| 2 | `Driver.c:199` | `DeviceEnumerateAll` → `DeviceEnum.c`（MADT/MCFG + 传统 0xCF8 扫描，只读，**不批量写 Command**，`DeviceEnum.c:4`） |
| 3 | `Driver.c:201` | `HalDriverRegister`：遍历 `.toy.driver.boot` 段（`HalDevices.c:22-47`），**按 `Order` 插入排序后逐个调用**（`DriverBoot.h:10-32`） |
| 4 | `Driver.c:203` | `DisplayHardwareInitialize`（第二次，见 K2-4） |
| 5 | `Driver.c:204` | `DriverProbeClass(DRIVER_CLASS_BLOCK)` ← 只 Block，注释 `:194-196` 说明「勿 ProbeAll：ps2-kbd 会跑 `Ps2InitHw`，真机无 8042 时 STATUS 浮空 → 死循环」 |
| 6 | `FileSystem` 模块 | `HalBlockInitialize`（`BlockAta.c:54-63`）再 `ProbeClass(BLOCK)` + `BlockInitialize`（`Library/Block/Block.c:42-75`） |
| 7 | `Usb` 模块 | `HalUsbInitialize` → `InputXhciInit` → `DriverProbeClass(INPUT)`：xHCI(70) → EHCI(80) → UHCI(90) → PS/2(100) |
| 8 | `Network` 模块 | `NetworkInitialize`（`Network.c:17-30`）：x64 只 `Udp/Tcp` 初始化，不 `HalNetInitialize`（`HalNetProbeAtModuleInit=0`） |
| 9 | Worker | `HalNetInitialize` / `HalIwlClaim` / `HalWifiClaim` / HDA（`Tasks.c:161-210`） |

`DRIVER_BOOT` 序值（`DriverBoot.h:10-32`）本身就是绑定契约：`AHCI=10 < ATA=20 < NVME=30 < USB_HOST=40 < MSC=60 < INPUT_XHCI=70 < INPUT_PS2=100 < NET=110 < IGPU=190 < HDA=200`。

### 4.2 顺序约束

- **Block 类在 VMM 之后才 Probe**（`Driver.c:194-196`、`BlockAta.c:56-60`）：AHCI/NVMe 都要 `VirtualMemoryEnabled()`（`Nvme Block.c:43-45`、`InputXhci.c:51-53`）。早 Probe 全部返回 -1，只能等 FS 模块的第二次机会。
- **xHCI 必须在 MSC 之前**：MSC 的 claim 用 xHCI 的 EP0/Bulk 环（`UsbMsc.c` 与 `ClaimPorts.c`）。`Order` 里 `USB_HOST=40` 在 `MSC=60` 前，但**真正的 Bringing up 发生在 `Usb` 模块**（`InputXhciInit`），MSC 在 FS 模块才 claim——跨模块的隐式依赖，注释写在 `FileSystem.c:7-11`。
- **PS/2 最后**（`Order=100`）：它是 `Match=0` 的自扫驱动（`Ps2.c:100-107`），只要 8042 应答就绑。因为 Input 是 **mux**（`Core/Driver/Input.c:12-32`，最多 4 个 backend 聚合，`DriverInputKeyboardDequeue` 逐个试，`:48-61`），PS/2 不会把 xHCI 挤掉——这点容易误判，值得在代码里写死（现在没有注释说明「后绑不会覆盖」）。
- **HDA 必须最后**（`Order=200`，且实际在 Worker 才 `HalHdaMmioInitialize`）：`HdaCodecInit` 要枚举 `STATESTS`、走 CORB/RIRB（`Codec.c:228-250`），任何一次超时都是数百 ms 级。

### 4.3 可疑点

- **D4-1 ⚠️ 块后端是全局单例，后注册者直接覆盖**：`BlockRegisterBackend` 只有一句 `gBackend = Backend`（`Library/Block/Block.c:14-16`），`BlockInitialize` 只枚举**这一个后端**的盘（`:51-60`）。于是「AHCI 装系统 + NVMe 装数据」的机器上，NVMe Bind（`Nvme Block.c:58-61`）晚于 AHCI（`Ahci Block.c:68`）→ **AHCI 的盘从系统里彻底消失**。`UsbMscMount` 更直接：`BlockMscInstall` 再覆盖一次。
  **外显症状**：串口 `block: no drives`（`Block.c:62-65`）或 `FS: Mounted N volume(s)` 里根本没有系统盘 → `FS: No Volumes Mounted` → 桌面起不来（停在 Logo 92%）。在只有一块盘的机器上永远不复现。
- **D4-2 ⚠️ xHCI 只绑第一颗控制器**：`gXhciInputDriver` 没有设 `.MaxInstances`（`InputXhci.c:190-198`），`InstanceLimit` 因此取 1（`Driver.c:35-40`）。`DriverProbe` 第二轮起直接 `if (gXhciReady) return 0;`（`InputXhciProbe.c:44-49`）。**外显症状**：双 xHCI 机器（Intel PCH + 第三方）上键鼠插在第二颗 → `Boot: Input NONE (Continue)`（`Usb.c:48-50`），只剩 PS/2 或干脆无输入。
- **D4-3 🟡 xHCI 的 fallback 只在虚拟机生效**：`InputXhciProbe.c:16-19` `if (Fallback == 0 || !HalCpuIsHypervisor()) return -1;`——Boot 侧 `FillXhci` 辛苦算出的 `XhciBaseAddress`（`FillXhci.c:124-134`，还会优先挑 QEMU 的 `1b36:000d`，`:106-109`）在真机上被**主动丢弃**，理由注释为「真机勿二次 Init（同 BAR 再 reset 会挂）」。合理，但意味着**真机没有第二条 xHCI 兜底路径**（与 D4-2 叠加）。
- **D4-4 🟡 `HalDriverRegister` 的 32 条上限**：`Item[32]`（`HalDevices.c:24`）与 `N < 32`（`:30`）是字面量，而 `DRIVER_BOOT` 已有 20 个使用者；且**超限静默丢弃**。**外显症状**：新增驱动后「注册了但不生效」，没有任何日志。
- **D4-5 🟡 `DeviceEnumerateAll` 与 `PciScanUSBControllers` 是两套 PCI 扫描**：`DeviceEnum.c`（新，填 `DEVICE_NODE`）与 `PCIe.c:105+`（旧，直接扫 256 总线找 USB 控制器，且**会写 Command 的 bit0-2**，`PCIe.c:60`）。`InputXhciProbe.c:56` 又用 `PciUsbAt` 重读一遍。三套扫描、两套语义，`PciUsbAt` 会**打开 MEM/IO/BME**（`:60`），`DeviceEnum` 不会。**外显症状**：某些设备在枚举阶段读不到 BAR，却能在绑定阶段读到——排查时很容易得出「枚举漏了设备」的错误结论。
- **D4-6 🟡 NVMe 的超时是「松弛忙等圈数」**：`Initialize.c:115-119` `TimeoutLoops = To * 500000u`（`To` 以 500ms 为单位，CAP.TO）。真机上 5s 的 `To` 会变成 50 万~250 万次 MMIO 轮询，**总时长不可预期**。**外显症状**：一块坏盘/半插的 NVMe 让 `Boot: NVMe Drives=` 之后卡 5~15s，表现为「开机在黑屏上停很久」，而且日志停在 NVMe 那行。

### 4.4 与注释不符

| 位置 | 注释说 | 代码做 |
| --- | --- | --- |
| `HalDevices.c:21` | 「块设备按 Order 登记：先 AHCI，再 ATA，再 NVMe。**后登记者 Probe 时可覆盖后端**」 | 这句是**唯一**承认「覆盖」语义的地方，但它写在注释里、不在 `BlockRegisterBackend` 的契约里（`Library/Block/Block.c:14-16` 没有一句警告）。D4-1 的坑正是「注释在 A 文件、语义在 B 文件」。 |
| `Driver.c:194-196` | 「只 Probe Block… Input/Net 仍由后续 usb/network 模块 Probe」 | `NetworkInitialize` 在 x64 下**不** Probe 网卡（`Network.c:20` 的 `HalNetProbeAtModuleInit` 为 0），网卡要到 Worker 才 `HalNetInitialize`（`Tasks.c:196`）。注释把「后续模块」说成了一个动作，实际是「后续模块 + Worker 两步」。 |
| `FileSystem.c:7-11` | 「3–4. 启动盘已有 BLOCKS 则 skip MSC；启动盘未挂（U 盘起）必 claim」 | 实际是 `FileSystem.c:353-412` 的三段判定 + `MscAutoBeforeFs`（`UsbMsc.c:269-300`），且 `FsBootDiskHasBlocksId` 返回 **-1**（旧 Boot 无 GUID）时走的是另一条分支（`MountVolume.c:57-73`）。三档语义比注释多一档。 |

---

## 5 · 文件系统挂载（ESP / BLOCKS / RES / ROOT.CFG / OsTarget / U 盘 claim）

### 5.1 做了什么（关键调用链）

```
FileSystemInitialize                      FileSystem.c:329
  HalBlockInitialize                      BlockAta.c:54           ← 第二次 Probe Class + BlockInitialize
  MountAllVolumes                         Mount.c:23
    for d: BlockSelect(d) → GptFindAllFat  Mount.c:37-52
      FsMountOneFatPart(d,&part,&BlocksVol) MountVolume.c:144
        GptReadPartInfo → 有 BLOCKS.ID ? NameBlocksIdVol : NameEspSlot   MountVolume.c:75-112
    FsMountAddResVolume()                 Mount.c:54             ← RES 合成卷（Synthetic）
    FsResolveOsTarget(&BlocksVol)         MountOs.c:226          ← ROOT.CFG → disk/part 钉死
    FsMountPickDefault(BlocksVol,...)     MountPick.c:122        ← 默认卷裁决
    FileSystemActivate(gDefaultVol)       FileSystem.c:38-71     ← Select(Ops) + BlockSelect(drive) + Mount(LBA)
  之后（桌面后）：FileSystemEnsureSecondaryVolumes  FileSystem.c:449-479
```

- **ROOT.CFG 的读取在 Boot 侧与内核侧各一次**：Boot 侧 `TryLoadDisplayPref` 只为选模读 `mode=`（`Theme.c:156-167`）；内核侧 `ParseRootCfg`（`MountOs.c:71-…`）只认 `disk=` / `part=`（`:111-121`）。两边都走 `BootSelectOsVolume` / `BootOsVolForPass` 的**四档认卷**（`Volume.c:502-556`，`BootPrivate.h:97-100`：0=ESP ROOT.CFG、1=同盘 BLOCKS.ID、2=它盘 BLOCKS.ID、3=启动卷）。
- **默认卷裁决**有四个输入：启动盘 GUID（`CacheBootDiskMatch`，`MountVolume.c:31-51`）、是否 MSC（`BlockDriveIsMsc`）、卷表是否满（`PickVolSlot`，`:114-141`）、以及 `gOsVolIdx` 的强制覆盖（`Mount.c:66-68`）。

### 5.2 顺序约束

- **认卷必须在 `gVolCount` 填满之后**：`FsResolveOsTarget` 遍历 `gVols[]`（`MountOs.c:…`），`CacheBootDiskMatch` 依赖 `BlockSelect(Drive)` 能成功（`MountVolume.c:41`）。把 `FsResolveOsTarget` 提到 `FsMountOneFatPart` 之前，得到的是空表 → 全部回落到 pass 3。
- **`BlockSelect` 是全局副作用，不能和读并行**：`Block.c:30-36` 改的是全局 `gDrive`，而 `GptFindAllFat`/`FsMountOneFatPart` 都靠它。`FileSystemLock`（`FileSystem.c:488-498`）就是为这个存在的（注释 `:481-487`）；`MountAllVolumes` 自己在锁外调用，靠「开机单线程」兜着。
- **`FileSystemActivate` 必须在 `FsMountPickDefault` 之后**：`Activate` 里 `Mount(StartLba)` 会重置 FAT 层状态（`FileSystem.c:61-65`）。顺序反了，默认卷指针指向的 FAT 缓冲是上一个卷的。
- **U 盘 claim 必须在 `MountAllVolumes` 之前**：`FsLatchMscBootDisk` / `MscAutoBeforeFs`（`UsbMsc.c:269-300`）要在卷表建立前把 MSC 盘挂上，否则 U 盘系统第一次挂载时看不到盘。`FileSystem.c:353` 的 `HalBlockInitialize() > 0 && MountAllVolumes()` 把两件事压在一行里，靠 `MscAutoApplyPolicy` 的先后（`:362-412`）保证。

### 5.3 可疑点

- **F5-1 ⚠️ ROOT.CFG 有两份语义不同的解析器**：Boot 侧 `Volume.c:401-427`（逐行扫，要求 `Len >= 5+36` 且**行首**就是 `disk`/`part`，拷满 36 字符后**忽略行尾内容**）；内核侧 `MountOs.c:71-124`（跳过前导空格、支持 `#` 注释、值缓冲 `Val[40]`、必须过 `GptParseUuidStr` 严格解析）。四处差异：缓冲上限 4096（`Volume.c:321`）vs **256**（`MountOs.c:127`）、行首空格、`#` 注释、行尾垃圾。**外显症状**：Boot 认 ROOT.CFG 从 A 盘起内核，内核的 `FsResolveOsTarget` 却因「行尾带注释」或「文件 >255 字节（几行注释就够）」判 ROOT.CFG 无效 → 默认卷落到 pass 1/2 的另一块盘；表现是「`Kernel.elf` 明明在 A 盘，桌面默认卷却是 B 盘」。两份都自洽，只是没人把语义写死在一处——这与 `BootPrivate.h:97-100` 定下的「四档认卷只写一次」纪律正好相反。
- ✅ 备案（**已修，别再按老结论改**）：Boot 侧 ROOT.CFG 的读法是对的——`Volume.c:364-392` 明确「先问大小再整读」，注释还把「固定小缓冲会被 `Read` 静默夹到剩余长度」写了下来，`ROOT_CFG_MAX_SIZE=4096`（`:321`）；`GetInfo` 的 128 字节续量只覆盖变长 `FileName[]`（这里是固定的 `\EFI\Blocks\ROOT.CFG`，41 字节），不会溢出。`1029` 的 PR-02 确实落地了。真正的残留在 F5-1 的「两份解析器」，不在缓冲区。
- **F5-2 🟡 四遍认卷 × N 个分区，每次谓词都整读文件**：`Volume.c:531-552` 的双层循环；`KernelPredicate` 会 `BootReadWholeFile` 整个 `Kernel.elf`（`Read.c:155`）。虽然命中即 `return`（`:549-550`），但**未命中的每一遍都要开卷/开文件/GetInfo**，U 盘（USB 2.0）上四遍就是四次几十 ms 级枚举。**外显症状**：`Boot: ReadKernelFile` 到 `CheckKernelFile` 之间停顿 0.2~1s（`BootDbg` 关掉时完全不可见）。
- **F5-3 ⚠️ `FsUpdateBootMediumState` 里的「NUC 起却 Present=0」分支是空的**（`MountOs.c:526-529`）：`if (!gMscIsBootDisk && !OsWasBootDisk && gBootDiskSeen) { /* 注释 */ }` —— **只有注释、没有语句**，紧接 `:529` 就 `gBootMediumGone = 1`。**外显症状**：NUC（内置盘启动）在某种热插拔序列后会误判「启动介质已移除」→ Clock 起 5s 大窗 → **自动关机**（`Clock` 侧逻辑）。这是本次评审里唯一「注释写了反例、代码没实现」的空语句块。
- **F5-4 🟡 卷表满时的挤占策略**：`PickVolSlot`（`MountVolume.c:114-141`）在 `FS_MAX_VOLUMES` 满时挤掉「最后一个非系统卷、非 RES 槽」，注释 `:118-121` 说这是为了「USB 系统卷永远挂得上」。**外显症状**：多 ESP/OEM 分区的机器上，Files 里的卷列表顺序与数量随插拔变化，用户看到「U 盘把别的盘挤掉了」。
- **F5-5 🟡 `MountAllVolumes` 失败与「0 卷」共用返回值 0**（`Mount.c:60-63` vs `:69-72`）：0 表示「没有卷」还是「激活失败」，调用方无法区分（`FileSystem.c:353` 只看真假）。**外显症状**：`FS: Mounted N volume(s)` 一行都不打，只有 `FS: No Volumes Mounted`，看不出是 GPT 没找到 FAT 还是 `Activate` 失败。
- **F5-6 🟡 失根判定用 5s 墙钟但基于 `HalTicksPerSec` 常量**：`UsbMsc.c:326-332`、`:417-422`，`Clock` 侧大窗也同源。与 H3-3 叠加：真机上「5 秒」可能是 3 秒或 8 秒。

### 5.4 与注释不符

| 位置 | 注释说 | 代码做 |
| --- | --- | --- |
| `MountOs.c:5-12` | 「开机 `ResolveOsTarget`：启动盘 ESP ROOT.CFG → 钉 Disk/Part」 | `ParseRootCfg` 的键名是 `disk=` / `part=`（`:111-121`），**不是** `ROOT.CFG` 里的 `disk/part` 之外还有别的入口；而 Boot 侧 `Theme.c` 读的是同一个文件里的 `mode=`。一个文件两套读者、两套键，文件头没写全。 |
| `Mount.c:5` | 「`MountAllVolumes` — Drive 扫 → GPT FAT → MountVolume → RES → 默认卷」 | 实际顺序是 `RES`(54) 在 `FsResolveOsTarget`(64) **之前**、`默认卷`(65) 之后还有一次 `gOsVolIdx` 覆盖（`:66-68`）。「RES → 默认卷」把两次裁决合并成一步，掩盖了 `gOsVolIdx` 的优先级。 |
| `MountOs.c:521-524` | 「注意：仅 Seen（NUC 起）且 MSC 不是启动盘时，Present 仍应为 1」 | 对应的分支是**空块**（F5-3），代码最终仍把 `gBootMediumGone` 置 1。 |
| `FileSystem.c:13` | 「多份 BLOCKS.ID：皆名 BLOCKS；默认卷跟 OsTarget（ROOT.CFG 可跨盘）」 | `NameBlocksIdVol`（`MountVolume.c:82-112`）有 6 条裁决分支，包括「启动盘 BLOCKS 优先」「内部盘优先于 USB」，还有一条「歧义时保留先挂上的」（`:108`）。「跟 OsTarget」只是其中一条。 |

---

## 6 · GUI 就绪（ThemeLoad / 字体 / WM / 桌面 / 光标 / BootLogo）

### 6.1 做了什么（关键调用链）

`Video` 模块（第一次，屏还只有 Logo 前的空白）：
```
Initialize/Video.c:25 VideoInitialize
  :29 FontInitialize → :30 ThemeInitialize        ← Theme 第一次（会把 scale 重置成 100，Theme.c:31）
  :31 HalVideoSet → :32 HalVideoEnableFbWc → :33 HalVideoInitializeBackbuffer
  :34-35 GraphicsSetPixel/FillRect
  :36 HalSerialGopEnable → :37 HalVideoLogFbPte
  :43 HalVideoClearScreen(0) → :44 HalVideoDiscardDirty     ← 注释 :38-42：铺黑后缓冲并丢弃脏区
  :45 DisplayHardwareInitialize (igpu forcewake/GTT/blit/present)
  :47 ThemeBootScaleApply                                   ← PR-28：先落最终缩放
  :48 DesktopBootLogoShow
```
`Gui` 模块（真正的桌面）：
```
WindowManager.c:238 GuiInitialize
  :239 PollInput
  :241 DesktopBootLogoSet(95)                ← 从 92% 走到 95%，每 1% 22ms
  :242 DbInitialize                         ← ⚠️ 见 G6-1
  :243 FontLoadAssets                       ← 列 Assets/Fonts/*.FNT
  :244-246 FontTtfLoad / FontTtfInit / FontTtfMonoInit
  :251 FontFtInit                           ← 注释 :247-250：必须早于 Locale 预热
  :253 FontNotifyTtfReady
  :255 ThemeLoad                            ← DB → THEME.CFG → FontSetById
  :256 LocaleInitialize
  :258-260 HalVideoSetUiScale(ThemeUiScale())
  :261-262 DesktopBootLogoSet(100) / DesktopBootLogoFinish   ← 内部再停 ~1s（BootLogo.c:355-366）
  :263 GuiDesktopInitialize
  :264 DrainMouse
WindowManager.c:107 GuiDesktopInitialize
  :108-113 取屏尺寸（0 则 1024×768 兜底，:110-113）
  :118-122 光标居中 + HalInputMouseHandoffDesktop（对齐驱动内 Abs，防「首动飞角」）
  :139-146 清窗口表
  :149 PreallocWindowBackups                ← 此刻无活动窗，实际是空转
  :150-161 DesktopInitialize / 首帧前同步灌 BMP（DesktopEnsureIconsLoaded）/ 登记 Ops
  :196-200 前缓冲整屏黑 + 抬高条带 + GuiRedraw
  :204-207 仅 !HalTimerUsesIrq 时提前 Reveal（Pi 路径）
  :209 BootLogFlush；:214 `Gui: Desktop Ready`
```

### 6.2 顺序约束

- **`FontFtInit` 必须早于 `LocaleInitialize`**：注释 `WindowManager.c:247-250` 写明「否则开机 stb HIT 占槽，Worker 再 Init 也 DropCjkHits 风暴 → 鼠死；不 Drop 则汉字永远旧 stb」。这是有历史现场的顺序约束（PR-21 的真因）。
- **`ThemeLoad` 必须晚于 `DbInitialize` + `FontLoadAssets`**：`ThemeLoad` 的 `ApplyDbKey("font")` 按字体名解析（`Fonts.c:464+` 的 `FontIdByName`），字体表没建就只有 Terminus。
- **`HalVideoSetUiScale` 必须晚于 `ThemeLoad`**：`ThemeUiScale()` 的值由 `ThemeLoad` 写入（`Config.c:96-100` 的 `scale` 键）。
- **`DesktopBootLogoFinish` 必须在 `GuiDesktopInitialize` 之前**：`Finish` 收尾并复位 Logo 状态（`BootLogo.c:355-379`），之后 `GuiDesktopInitialize:196-200` 整屏黑 + `GuiRedraw` 直接盖住。反过来，桌面会被 Logo 的九宫格压在下面。
- **`GuiDesktopInitialize` 必须在 `FontSetById(ThemeFontId())`（`Kernel.c:120`）之前？** 不——两者顺序无关：`KernelAfterModules` 在整个模块表之后，此时 Gui 模块已按 DB 设过字体，`FontSetById` 只是**把 boot GOP 镜像关掉后再套一次**（`Theme.c:42-47` 的注释「boot GOP 镜像勿套桌面字」）。

### 6.3 可疑点

- **G6-1 ⚠️⚠️ `DbInitialize` 在 FileSystem 之前被闩成空库（本次评审第一优先）**
  - 链路：`VideoInitialize`（模块表第 4 项，`Modules.c:48-49`）→ `ThemeBootScaleApply`（`Live.c:77`）→ `DbInitialize`（`Live.c:81`）→ `DbLoad`（`Database.c:237`）→ `FileSystemReadFile("BLOCKS.DB")`（`File.c:75`）。此刻 `gVolCount == 0`，`FileSystemResolve` 直接 `return FAT_ERR_IO`（`FileSystem.c:89-91`），于是 `DbLoad` 返回 `DB_NOENT`。
  - 闩：`Database.c:238-241` 无论 `Err` 是什么都 `gReady = 1`；`DbInitialize:233-235` 下次直接 `return DB_OK`。所以 `GuiInitialize:242` 的那次 `DbInitialize` **什么也不做**，`ThemeLoad` 拿到的是空表 → `FromDb == 0` → 回落到 `THEME.CFG`（`Config.c:102-105`）。
  - 注释与现实的冲突：`Live.c:74-75` 写「`DbInitialize` 幂等，这里提前初始化不影响后续」——**它不幂等，它是一次性闩**。
  - **外显症状**（三档，按严重度）：
    1. 串口里 `db: ready records=0`（`Database.c:248-250`）出现在 `***** Module Video *****` 之后、`***** Module FileSystem *****` 之前 → 坐实。
    2. BLOCKS.DB 里的 `scale=` / `font=` / `theme=` / `wallpaper=` / `lang=` **本次会话全部不生效**；PR-28 的「第一帧就是最终缩放」也因此没有生效（`ThemeBootScaleApply` 在 `DbGet` 就返回 -1，`Live.c:84-86`，连 `Theme: boot scale applied` 那行都不会打，`:94`）。
    3. 更重的一条：`DbSave` 是**按内存表整份重写**（`File.c:98-146`，注释 `:141` 自认「同名覆盖写出」）。若用户在 Settings 里改任何一项 → `DbSet` → `DbSave` 把 `BLOCKS.DB` 覆盖成「只剩新键」→ **原来的 `mode=`/别名全丢**，下次开机分辨率也回默认。
  - 修法只需一处顺序（`DbInitialize` 等到 `FileSystem` 之后第一次用再触发，或让 `gReady` 只在 `DbLoad` 成功时置 1），但会牵动 `ThemeBootScaleApply` 的存在意义——需要单独一刀，不能夹在别的 PR 里。
- **G6-2 🟡 `GuiInitialize` 全程吞返回值**：`:242-257` 八个调用全是 `(void)`，`:266` 注释「软成功：库/主题/字缺失不挡进桌面」。**外显症状**：桌面起来了但标题栏/正文全是 `Terminus` 或方块，串口里既没有 WARN 也没有 ERR（`Font: no Assets/Fonts/*.FNT` 走 `LibWrite`，`Fonts.c:452-454`；TTF 缺失走 `LogDrv`，`TrueTypeLoad.c:211-213`）——`LogDrv` 在课堂构建里通常不上串口。
- **G6-3 ⚠️ BootLogo 的计时是「按 ~3GHz 估」**：`LOGO_TSC_PER_MS = 3000000ull`（`BootLogo.c:28`），`LogoSpinMs`（`:75-83`）用 `rdtsc` 算毫秒。`DesktopBootLogoSet` 每个 1% 都要 `LogoSpinMs(22)`（`:344-352`），`DesktopBootLogoFinish` 再 `LogoHoldUntilMin`（`:355-366`，`LOGO_MIN_MS=1000`，`:27`，注释 `:26` 自认「按 ~3GHz 估」）。**外显症状**：主频 2.0GHz 的机器上「1 秒」变 1.5 秒、2.4GHz 变 1.25 秒；4.5GHz 变 0.67 秒。叠加 `Modules.c:135-152` 的档位，**真机开机总时长会随机器变化**，而 QEMU（约 3GHz 等效）永远"正常"。这条同时是「为什么 CI 看不出开机慢」的一半答案。
- **G6-4 ⚠️ `GuiDesktopInitialize` 首帧前同步读盘**：`:161` `DesktopEnsureIconsLoaded()`，注释 `:158-160` 说明「`ModulesRun` 时尚无 Worker 并发，读盘安全」。确实安全（Worker 未创建），但它把 **BMP 解码 + FAT 读**放进 `Gui` 模块的**同步路径**，直接加进「到 `Gui: Desktop Ready` 的时间」。**外显症状**：图标多的 rootfs 上 `Gui: Desktop Ready` 明显推迟（与 `Boot: Logo Done` 之间的间隔）。
- **G6-5 🟡 100% 缩放与 200% 缩放走两条不同的像素路径**：`GuiDesktopInitialize:196-200` 用 `HalVideoDrawBeginFront/FillRect/DrawEndFront` **直接写前缓冲**，而 `GuiRedraw` 走的是后缓冲 + `PresentFlush`。注释 `:190-195` 的理由是「避免 U-Boot/boot 字与桌面条带撕开」。**外显症状**：如果 `GuiRedraw` 因任何原因（OOM、字体缺失）少画一块，屏幕上留下的是纯黑，不是桌面底色——比「画错」更像「死机」。
- **G6-6 🟡 `HalVideoInitializeBackbuffer` 失败静默降级**：`HalVideo.c:37-66`，`PhysicalMemoryAllocatePages` 失败就 `return`，注释 `:33-35` 自认「分配失败则保持直写 GOP（功能仍可用，仍可能撕裂）」。**外显症状**：4K（33MB）+ 低内存机器上桌面持续撕裂/闪，没有任何日志。
- **G6-7 🟡 `ThemeInitialize` 被执行两次且语义是「重置」**：`Kernel.c:112` 与 `Initialize/Video.c:30`。第二次会把 `gThemeUiScale` 拍回 100（`Theme.c:31`）、`gWallpaper=0`（`:36`）、`gFontId=0`（`:28`）。当前两次都在 `ThemeBootScaleApply` 之前，恰好无害；**一旦有人在中间插一次 `ThemeLoad`/`ThemeSetUiScale`，就会被第二次 `ThemeInitialize` 静默抹掉**。这是「靠顺序侥幸正确」的典型。

### 6.4 与注释不符

| 位置 | 注释说 | 代码做 |
| --- | --- | --- |
| `Live.c:74-75` | 「（`DbInitialize` 幂等，这里提前初始化不影响后续）」 | 一次性闩（`Database.c:233-241`），且失败也置 `gReady`。见 G6-1。 |
| `Live.c:69-72` | 「以前 ThemeLoad 在 GuiInitialize 里才把 scale 落到 HalVideoSetUiScale，而 Logo 早在 VideoInitialize 就以 100% 画过一次」 | 修法本身（`ThemeBootScaleApply`）没问题，问题是它读的 DB **这时还没挂载**——PR-28 的验证记录也写着「真机 50%/200% 冷启动需复验」（`git log 69c14d67`）。 |
| `BootLogo.c:26` | 「至少约 1 秒（按 ~3GHz 估），读盘已久则 Finish 几乎不再空等」 | `LogoHoldUntilMin` 的起点是 `gLogoTsc`（`:364`），只在 `Finish` 里设；`DesktopBootLogoSet` 的 `gLogoTsc` 在 `Show` 里初始化（`:298` 段）。「读盘已久」并不会减免这 1 秒，因为起点被重新拍了。 |
| `BootLogo.c:363` | 「合成后的九格再停大约一秒；**勿清黑**——`GuiDesktopInitialize→Redraw` 会直接盖住」 | `GuiDesktopInitialize:196-200` 确实先整屏黑再 `GuiRedraw`。所以「不清黑」只是少一次全屏写，并不能避免中间那一帧全黑。 |
| `BootLogo.c:4` | 「须在 igpu 探针之后调用，否则翻页/探针会跳帧」 | `VideoInitialize` 的顺序是 `DisplayHardwareInitialize`(45) → `BootLogoShow`(48)，**符合**；但 `Driver.c:203` 又把 igpu 探针跑了一遍，Logo 已上屏。注释没覆盖「第二次探针」。 |

---

## 7 · 任务与 Worker（Scheduler → Shell/Worker → 提示符）

### 7.1 做了什么

```
KernelMainCommon                       Kernel.c:149-156
  SchedulerCreate("Shell", ShellTask)  :150
  SchedulerCreate("Gui",   GuiTask)    :151
  SchedulerCreate("Worker",WorkerTask) :152
  KernelTaskDemoStart()                :153   （默认关）
  KernelTrySpawnInput()                :154   （Count≥4 才建）
  SchedulerStart()                     :155   （不返回）
SchedulerStart                          Core/Scheduler/Start.c:133
  :146-153 每核建 Idle 并钉核
  :156-196 按名字前缀钉核：'in'→InputCpu / 'sh'|'gu'→InteractiveCpu / 'wo'→CPU0
  :198-216 选 First（跳过钉在 AP 上的任务）
  :227-231 HalIrqDisable + Activate(First) + Online=1
  :236 HalTimerStart
  :244 HalSchedulerEnter（iret 进 First）
```
Worker（`Tasks.c:141-289`）的 15 步在 §6/§4 已列；到 ready 的最后三步是：iwl 静默窗（`:233-258`）→ `GuiCursorRevealAfterUsb`（`:264`）→ `gConsole.AnnounceBootReady()`（`:265`）。

真正的 `Blocks>` 打印（`Console.c:86-96`）：`AnnounceBootReady` → `HalConsoleWriteSerial(MSG_CON_READY)` + hint 行 → `Prompt()`（`:127-128` 打 `Blocks> `）→ `HalSerialShellOwn()`（`:95`，此后所有 `Log*` 不再上串口，只进 ring）。

### 7.2 顺序约束

- **Create 必须在 Start 之前**：`SchedulerStart:202-215` 只从**已存在**的 `gTasks[]` 里挑 `TASK_READY`，`Kernel.c:15` 的注释把这条写死了（「四环 Create 在 Common，不在 MODULE_PHASE_SESSION」）。
- **钉核必须在 `Online=1` 之前**：`:165-195` 在 `:231` 之前；`SchedulerApStart:90-92` 的 `while (!gSchedulerOnline)` 之后直接读 `IdleTaskForCpu(Cpu)`（`:94`）。钉核放在 Online 之后，AP 会拿到一个还没被挂到该核空闲槽的 Idle。
- **`HalTimerStart` 必须在 `SchedulerEnter` 之前**（`:236` 在 `:244` 前）：注释 `:238-243` 解释得比代码更细——**`cli` 要保持到 iret**，否则定时器会在 `gEarlyStack` 上入 Shell，live 帧 SP 落在 early 栈 → `sched: corrupt resume`（注释自认「CI/本地冒烟稳复现」）。
- **Shell 不能在启动时打 Prompt**：`Shell.c:5-6` 与 `:49` 注释「`ShellTask` 启动勿 `Prompt`（否则夹在 iwl/dhcp 黄字中间）」。真正的 Prompt 只由 `AnnounceBootReady` 触发。
- **`HalSerialShellOwn` 必须在 Prompt 之后**（`Console.c:94-95`）：反了则 Prompt 自己被切断（`HalSerialWrite.c:163-183` 里 `gShellOwnUart` 会挡掉串口写）。

### 7.3 可疑点

- **T7-1 ⚠️ Worker 的「iwl 静默窗」用裸 `rdtsc` 且假设 3GHz**：`Tasks.c:233-258`，`Now - From < 1500000000ULL`（注释 `:253` 自认「约 3GHz：0.5s=1.5e9」）、封顶 `6e9`（2s）。**外显症状**：主频低的机器上「静默窗」被拉长（0.5s→0.75s），高主频机器上提前结束 → `GuiCursorRevealAfterUsb` 早于 iwl 收尾 → 露光标后鼠标被 iwl 抢 xHCI，表现为「光标出来了但拖着走/卡顿」。这段整个 `#if defined(__x86_64__) && !Hypervisor`（`:233-234`），**QEMU 直接跳过**，所以 CI 永远看不到。
- **T7-2 ⚠️ 真机上提示符早于「系统就绪」**：`KernelAfterModules`（`Kernel.c:119-132`）在 `!Hypervisor && !ConsoleOnly` 时就 `AnnounceBootReady`，而这一切发生在 `KernelMainCommon`（`Kernel.c:149`）**建任务之前**。Worker 的 15 步（含 MSC claim、有线 Probe、iwl、DHCP、HDA）都在这之后。**外显症状**：用户看到 `Blocks> ` 立刻敲 `files` / `ip`，拿到的是「盘还没挂全 / 网还没起来」的结果；输入本身不丢（`Serial` 有 512 字节软环，`Serial.c:25-50`，RX IRQ 已在 `Cpu` 模块打开，`Initialize/Cpu.c:26-27`），但语义上「ready」名不副实。virt 路径反而是对的（等 Worker 末，`Tasks.c:265`）。
- **T7-3 🟡 钉核靠任务名首两字母**：`Start.c:169-194`（`NameStarts`，`:39-55`）。**外显症状**：任何新任务名叫 `Init...`/`Input...` 会被钉到 CPU2 并提成 `DEFAULT` 优先级；叫 `Worker...` 会被钉 BSP。命名即策略，且**改名不会有编译错误**——`Kernel.c:150-152` 的字面量是唯一真相，两处靠人工同步。
- **T7-4 🟡 `HalCpuCount() < HAL_INPUT_CPU_MIN` 时 Input 不建，但门闩散在四处**：`Kernel.c:47`（不建）、`Tasks.c:66`（Yield 里补 Poll）、`Hid.c:128`（键盘泵里补 Poll）、`SmpBoot`/`Irq.c:13`（IRQ 目的核）。`Hal.h:150-155` 的注释把这张表列全了，但**四个门闩必须同时改**，现在没有任何编译期约束（只有 `Hal.h` 一个 `#define` 被四处读）。
- **T7-5 🟡 HDA 明明叫「defer」，却排在光标 reveal 之前**：`Tasks.c:157-165` 的 `sSecondaryOnce` 块里有 `HalHdaMmioInitialize/CodecInitialize/StreamInitialize`，而 `GuiCursorRevealAfterUsb` 在 `:264`。`:160` 的注释写「旁路盘后认声卡（**不挡进桌面**）」，`:123` 的优先级表自己写着「2 `EnsureSecondary + HDA`（**仍无光标**）」。**外显症状**：机箱里没有 HDA codec 的机器上，`HdaCodecInit` 要等 `STATESTS` 与 CORB 超时，`Boot: hda codec none` 之前光标不出——与「不挡进桌面」的承诺相反。
- **T7-6 🟡 `DesktopWorkerTick` 每圈都跑，但 MSC 热插拔节流靠 `HalTicksPerSec`**：`Tasks.c:144` → `Clock` 侧 → `UsbMscHotPoll`（`UsbMsc.c:417-422`）。与 H3-3 同源。
- **T7-7 🟡 `WorkerTask` 的 15 步里 6 个是一次性 `static` 开关**（`sSecondaryOnce`/`sNetOnce`/`sIwlClaimOnce`/`sWifiClaimOnce`/`sBluetoothOnce`/`QuietArm`，`Tasks.c:155/192/193/205/235/278`）：全部**不可重置**，失败一次就永远不再试。**外显症状**：有线网第一次 Probe 失败（如 PHY 慢）→ 本次会话网络永远不起来，`ip` 显示 no link，重试只能重启。

### 7.4 与注释不符

| 位置 | 注释说 | 代码做 |
| --- | --- | --- |
| `Tasks.c:160` | 「boot-hda-defer：旁路盘后认声卡（**不挡进桌面**）」 | 在 `:264` reveal 之前跑，且需要等 codec 超时（`Codec.c:238-249`）。同文件 `:123` 自己承认「仍无光标」。 |
| `Tasks.c:134` | 「13 `GuiCursorRevealAfterUsb` → `AnnounceBootReady`」 | 真机路径上 `AnnounceBootReady` **早在 `KernelAfterModules` 就打过了**（`Kernel.c:129-131`），Worker 这一步是空转（`Console.c:87-89` 的 once 闩）。两个入口、一处生效，注释只写了 Worker 那条。 |
| `Console.c:4-8` | 「模块表 `ConsoleInitialize`（软成功：恒 return 0）… `PublishAll` → `StorePathGuard` → `Locale` → … → `gConsole.Initialize`」 | `ConsoleInitialize`（`:366-379`）还多做了 `ShellCommandsRegister` 的虚/真分叉（`:371-375`），且 `UserAliasLoad`（`:376`）**读的是 DB**——此时 FS 未挂、DB 未加载（若 G6-1 已发生则永远为空），用户别名在开机时**一定为空**。 |
| `Start.c:10` | 「CPU0 — Worker；CPU1 — Shell+Gui（≥2）；CPU2 — Input（≥`HAL_INPUT_CPU_MIN`）」 | `:157-158` 在 `Cpus==1` 时把三者并到 CPU0，`:160-163` 再被 `HalConsoleOnly/PinInteractive` 覆盖成 0。表格没列「单核」与「ConsoleOnly」两列。 |

---

## 8 · 阶段 → 入口函数 → 主要文件（总表）

| # | 阶段 | 入口函数 | 主要文件 | 失败外显 |
| --- | --- | --- | --- | --- |
| 1 | UEFI 串口/交接 | `UefiMain` | `Boot.c`、`Serial.c`、`FillRsdp.c`、`FillXhci.c`、`Volume.c:100` | 无输出 / 无 ACPI / 无 xHCI 兜底 |
| 2 | 选模 | `GetVideoInfo` → `SetVideoMode` | `Video/Video.c`、`Video/Score.c`、`Video/Edid.c`、`Video/Theme.c` | 分辨率不对 / 黑屏（`BltOnly`） |
| 3 | 认卷 | `BootSelectOsVolume` | `Volume.c:502`、`LoadKernel/Read.c:133-228` | 从错的盘起内核 / ROOT.CFG 静默失效 |
| 4 | 装内核 | `BootLoadKernel` | `LoadKernel/LoadKernel.c`、`Elf.c`、`Read.c` | `CheckKernelFile Failed` / AllocatePages Failed |
| 5 | 交棒 | `JumpToKernel` → `KernelEntry` | `JumpToKernel.c`、`X64/KernelEntry.S`、`X64/KernelHandoff.c` | 退回 UEFI / 静默重启 |
| 6 | 早期视频 | `KernelAttachEarlyVideo` | `BringUp/Kernel.c:102`、`Initialize/Video.c` | 屏上无字、Logo 不出 |
| 7 | 内存 | `PhysicalMemoryInitialize` → `VirtualMemoryInitialize` | `Core/PhysicalMemory/*`、`Core/VirtualMemory/VirtualMemory.c` | **静默重启**（无 IDT） |
| 8 | CPU/HAL | `CpuInitialize` → `HalInitialize` → `ArchInit` | `Hal.c`、`Architecture.c`、`Dispatch.c`、`IoApic.c` | Park 在 `Module Cpu Failed` |
| 9 | 驱动枚举 | `DriverInitialize` | `Core/Driver/Driver.c`、`HAL/X64/DeviceEnum/*`、`PCIe/PCIe.c` | `block: no drives` |
| 10 | SMP | `HalSmpStartApplicationProcessors` | `SmpBoot/SmpBoot.c`、`SmpBoot/Start.c` | `Smp: AP Timeout`、键鼠迟滞 |
| 11 | 输入 | `UsbInitialize` → `InputXhciInit` | `InputXhci*.c`、`Ps2/Ps2.c`、`Core/Driver/Input.c` | `Boot: Input NONE` |
| 12 | 调度/命令表 | `SchedulerInitialize` → `ConsoleInitialize` | `Core/Scheduler/Scheduler.c`、`Shell/Console/Console.c` | 提示符无命令 |
| 13 | 文件系统 | `FileSystemInitialize` | `Services/FileSystem/*`、`Library/Block/Block.c`、`HAL/X64/Drivers/Block/*` | `FS: No Volumes Mounted` |
| 14 | GUI | `GuiInitialize` | `Gui/WindowManager/WindowManager.c`、`Look/Fonts/*`、`Look/Theme/*`、`Desktop/BootLogo.c` | 豆腐块 / 主题丢失 / 无缩放 |
| 15 | 网络（模块内） | `NetworkInitialize` | `Net/Network/Network.c` | 无（真机挪到 Worker） |
| 16 | 任务 | `KernelMainCommon` → `SchedulerStart` | `BringUp/Kernel.c:149`、`Core/Scheduler/Start.c` | Park / 单核 |
| 17 | Worker | `WorkerTask` | `Services/Tasks/Tasks.c`、`Msc/UsbMsc.c`、`Hda/*` | 光标不出 / 不 ready |
| 18 | 提示符 | `AnnounceBootReady`（`Kernel.c:129` 或 `Tasks.c:265`） | `Shell/Console/Console.c:86`、`Tasks/Shell.c` | 无 `Blocks>` / 输入无回显 |

---

## 9 · 按风险排序的前 10 个问题

排序依据：**能不能把机器停在开机线上** > 是否吞掉用户配置 > 是否让功能静默缺失 > 是否只影响观感。每条给「证据 → 症状 → 建议」。

| # | 问题 | 证据 | 症状 | 建议（不改代码，只说方向） |
| --- | --- | --- | --- | --- |
| **1** | **`DbInitialize` 在 FS 挂载前被闩成空库**，随后 `DbSave` 可能整份覆盖 `BLOCKS.DB` | `Live.c:81` → `Database.c:233-241` → `File.c:75`；顺序见 `Modules.c:48-49` vs `:70`；`File.c:98-146` | BLOCKS.DB 全部配置本次失效（含 PR-28 的缩放）；Settings 一保存就把 `mode=`/别名写丢 | 只在 `DbLoad` 成功时置 `gReady`，或把首次初始化推到 `FileSystem` 之后；`ThemeBootScaleApply` 改成「FS 已挂才 apply，否则留到 ThemeLoad」 |
| **2** | **EARLY 阶段（含 VMM 开分页）没有 IDT** | `Architecture.c:188` 在 `ArchInit`（Cpu 模块）里；`VirtualMemory.c:327-330` 在 VMM（第 3 个模块）里 | 映射错 = 三重故障 = **静默重启/黑屏**，没有 `#PF CR2`，没有最后一行日志 | 把最小 IDT（至少 `#PF`/`#GP`）提到 `KernelHandoff` 里 `EnableEarlyIdentityMap` 之前 |
| **3** | **`ArchInit` 自 IPI 失败即整机 Park** | `Architecture.c:190-202` → `Initialize/Cpu.c:11-13` → `Kernel.c:135-137` | 串口停在 `***** Module Cpu Failed *****`、Logo 停在 20%，无原因 | 加 2~3 次重发 + 把 `gArchIrqCount` 打到 `BootLog`；失败改成「继续但降级」而不是 Park |
| **4** | **块后端单例，后注册覆盖；双盘机器只认 NVMe** | `Library/Block/Block.c:14-16`、`:51-60`；`DriverBoot.h:11-13`；`HalDevices.c:21` 的注释 | `block: no drives` 或挂错盘 → `FS: No Volumes Mounted` → 桌面起不来 | 后端改成「按盘号路由」的表（AHCI/NVMe 各占一段 drive 号），而不是一个全局指针 |
| **5** | **SMP AP 超时是 `pause` 计数** | `SmpBoot/Start.c:28-30`、`:136-155` | `Smp: AP Timeout`；`gCpuCount` 掉到 1 → 不建 Input（`Kernel.c:47`）→ 键鼠迟滞 | 用 LAPIC/TSC 墙钟超时（≥100ms），超时后**重试一次 SIPI**再放弃；并把最终 `CpuCount` 打进 BootLog |
| **6** | **两处 `rdtsc`/TSC 按 3GHz 估时的常量** | `Tasks.c:242-257`（1.5e9 / 6e9）、`BootLogo.c:26-28`（3e6）、`:75-83` | 真机 ready 时间与 Logo 动画随主频漂移；高主频机器上光标早于 iwl 静默 → 鼠标卡顿 | 开机测一次 TSC/墙钟比（`LAPIC` 一拍 vs `rdtsc`），得到 `TscPerMs`，两处都用它 |
| **7** | **`GuiInitialize` 全软成功 + `BootDbg` 在 release 下为空** | `WindowManager.c:242-257`、`:266`；`BootPrivate.h:14-21` | 字体/主题/DB 缺失时桌面「能起但不对」，串口无 WARN | 至少把「字体数 / TTF 数 / DB 记录数 / 主题来源」四条打成一行 `BootLog`（不进 `BootDbg`） |
| **8** | **`JumpToKernel` 8 次重试无诊断、无收敛依据** | `JumpToKernel.c:52-92` | 某固件上退回 UEFI，只有一行 Failed | 每次失败打 `%r` + `MapSize`/`MapKey`；区分「MapKey 失效」与「固件拒绝」并分别处理 |
| **9** | **`xHCI` 只绑第一颗 + 真机禁用 Boot fallback** | `InputXhci.c:190-198`（无 `MaxInstances`）→ `Driver.c:35-40`；`InputXhciProbe.c:16-19`、`:44-49` | 双控制器机器上键鼠全无（只剩 PS/2） | 给 xHCI `MaxInstances=2`，第二颗在「第一颗无 HID」时再试；或把 `FillXhci` 的 BAR 作为真机候选（只 Init，不 reset） |
| **10** | **Worker 里 6 个「一次性开关」不可重置：首次失败 = 本次会话永久缺失** | `Tasks.c:155`（Secondary）、`:192`（Net）、`:193`（Iwl）、`:205`（Wifi）、`:235`（QuietArm）、`:278`（BT） | 有线 PHY 慢一拍 → 本次会话网络永远 no link，`ip`/`ping` 全废，只能重启 | 改成「失败后指数退避重试 N 次」的计数器，而不是 `static int once`；并把最终成功/失败打进 `RUNTIME.LOG` |

**如果只能修一条**：第 1 条。它不需要动任何驱动/时序，收益是「一整类配置失效 + 一次潜在的数据丢失」，而且能顺手让 PR-28 的验证有意义（否则「首帧就是最终缩放」永远只能靠 `THEME.CFG` 的旧路径生效）。第 2 条更「底层」，但它是「让故障可见」的投资，不是「让故障不发生」——按当前优先级，先修 1，再把 2 作为下一天的独立刀。

---

## 附 · 复核用的日志判据（不改代码即可判定）

| 要判的事 | 看哪一行 | 位置 |
| --- | --- | --- |
| DB 是否在 FS 前被读空 | `db: ready records=0` 出现在 `***** Module FileSystem *****` **之前** | `Database.c:248-250`、`Modules.c:168` |
| PR-28 是否真生效 | `Theme: boot scale applied` 是否出现 | `Live.c:94` |
| 选模判据（PR-08 口径） | `Boot: GetVideoInfo Done (Pick WxH Mode N)` 三项 | `Video.c:236` |
| 是否走了保留模式 | `Boot: Keep Firmware Mode WxH (No mode=)` | `Video.c:104` |
| 内核入口/交接是否正常 | `Boot: Kernel Loaded, Entry=0x…` → `Boot: JumpToKernel Entry=0x…` | `LoadKernel.c:35`、`JumpToKernel.c:37` |
| AP 是否掉了 | `Smp: AP Timeout APIC=` | `SmpBoot/Start.c:150` |
| 块后端是否被覆盖 | `block: drive N ready` 的 N 与 `Boot: NVMe Drives=` | `Block.c:56-58`、`Nvme.c:80-88` |
| 是否挂上系统卷 | `FS: Mounted N volume(s), default=…` | `Mount.c:74-83` |
| U 盘 claim 是否成功 | `Boot: MSC Auto claim abort` / `Boot: MSC Auto Mux OK` | `UsbMsc.c:274-292` |
| GUI 是否就绪 | `Boot: Logo Done` → `Gui: Desktop Ready` | `BootLogo.c:366`、`WindowManager.c:214` |
| 是否 ready | `hint: type commands in THIS terminal` 紧接着 `Blocks> ` | `Console.c:91-94`、`:127` |
