# 结构收口 — PR 拆分

> 状态：71 刀已列入 `Documents/路线图新.md`「结构收口」。★ 仍无。还没改代码。
>
> 依据是 2026-10-17 的源码，不是技术手册。手册仍写着 `Common/`、`Library` 里有 Fat/Elf/UI，和树对不上。
>
> 对标：依赖只朝硬件走；一个协议一份实现；私有头不靠搜索路径撞名；门面按能力拆开；像素走已有的几何库，送屏留在 HAL。实现保持薄，每刀不改协议行为。

刀号不在同一节。打开本文件后按下面找：

| 刀号 | 在哪一节 |
| --- | --- |
| 1–18，插刀如 4A | 「刀」。插在两刀之间用 4A、4B，不把后面的号码往后挪 |
| 19–42 | 「远期」 |
| 43–48 | 「汉字与语言」 |
| 49–50 | 「适合放进 Library 的」 |
| 51–62 | 「第 51 刀起」 |
| 63–71 | 「第 63 刀起」 |

第 60 刀在「第 51 刀起」，路线图新里序号也是 60：`PR-ELF-boot-kernel`。Boot 和内核各留一份 ELF 头，加对照，不合成一个文件。还没开 ★，也还没改代码。

这一轮接着读：EHCI 的 `SchedStart` 与 `EnumHid`、Shell 的 help/ls/exec、任务栏网络托盘、FAT 删文件是否放簇、iwl 的 MIC。读完写进白皮书。

## 代码里实际的层

约 13.0 万行（`.c` / `.h` / `.S`，不含 `Build/`、`ThirdParty/`）。

| 目录 | 行数 | 实际职责 |
| --- | --- | --- |
| `HAL/` | 5.6 万 | 三架构板级，加上 x86 驱动。xHCI 6800、EHCI 3800、UsbHid 2200 |
| `Services/` | 3.4 万 | 桌面、窗口、控制台、商店、设置。约 60 个文件直接调 `HalVideo*` |
| `Core/` | 1.2 万 | 内存、调度、进程、系统调用、FAT 实现、设备表 |
| `Library/` | 1.2 万 | 其中字模约 9600 行。算法代码约 2000 行：`CString`、`Block`、`Gpt`、`Gfx`、`Text`、`Vfs` |
| `User/` | 0.8 万 | 用户态程序和系统调用包装 |
| `Include/` | 0.7 万 | 对外头。`Hal.h` 一个头里有 CPU、中断、页表、ELF、平台、端口 |
| `BringUp/` | 0.1 万 | `KernelMain` 和三张模块表 |

`Library/` 有纯度检查，`Text` / `Gfx` 不调 `Hal*`。PCI 驱动已填匹配表。`Iwl/Hw` 与 `Iwl/Mac` 是现在的树，旁边没有第二份旧目录。新代码基本是 PascalCase。超过 300 行的实现文件，去掉三份字模后只剩约十个；不把「按行数再拆一遍」当作本柱。

## 依赖方向

允许的方向：`BringUp` 可以调用各层（它是组合根）。`Services` 可以调用 `Core`、`HAL`、`Library`。`Core` 可以调用 `Library`，以及窄的硬件能力。`HAL` 可以调用 `Library`。`Library` 不能包含 `Hal.h`、`Core`、`Services`，也不能调用 `Hal*`（`Makefile` 的 `check-library-pure` 已经查这个）。

2026-10-17 按 `.c` 的 `#include "..."` 统计跨层次数。同层包含不计。

| 从 | 到 | 次数 | 主要头 |
| --- | --- | ---: | --- |
| HAL | Core | 262 | `ToySerialLog.h` 79、`VirtualMemory.h` 53、`PhysicalMemory.h` 50、`Debug.h` 40、`BootInfo.h` 10、`Scheduler.h` 7 |
| Services | HAL | 156 | `Hal.h` 72、`HalVideo.h` 45。这是朝硬件走，允许 |
| Services | Library | 112 | 允许 |
| Services | Core | 101 | 允许 |
| HAL | Services | 30 | `Udp.h` 10、`Tcp.h` 10、`LwIp.h` 4、`Console.h` 2、`Theme.h` 1 |
| Core | Services | 29 | `Console.h` 16 个文件、`LwIp.h`、`Gui.h` 3 个文件、`Store.h`、`Theme.h`、`Tasks.h` |
| Core | HAL | 27 | 其中 25 个是 `Hal.h` |

反向里已经有刀的：Core 的 `Console.h` 是刀 3，`Gui.h` 是刀 4，`Theme.h` 是刀 5，文件系统对 Shell/商店的包含是刀 6。HAL 收包直接包含 `Tcp.h` 是刀 11 和远期 32–34 要收的边界：分发可以留在 HAL 里调用一个 Core/Services 的入口，但不要让网卡文件认识 TCP 的私有头。`Debug.h` 包含 `Hal.h` 是刀 19。

`Hal.h` 被 Core 和 Services 当成总门面，所以「Services → HAL 156」里有 72 次并不是 72 种硬件能力，而是一个头。刀 14 先拆声明，不在这一刀改调用点。

## 适合放进 Library 的

`Library` 里已经有：`memcpy` / `memset` / `memmove` / `memcmp` / `strlen` / `strncmp` / `strtol`（`CString.c`），数字转文本（`Text.c`），图形（`Gfx`），GPT，VFS 接口。FAT 的头已经在 `Include/Library/`，实现还在 `Core/FileSystem/Fat/`，搬移是刀 7，不再把 FAT 的 `Read16` 单独抽出去（全仓库只有 FAT 在用）。

还没进去、而且不依赖 HAL 的，只有字符串这一小块：

| 现在 | 在哪 | 和 Library 的差别 |
| --- | --- | --- |
| `StrEq` | `ConsoleCmd.c` 里是**全局**函数。`Device.c`、`DeviceDump.c`、`DriverMatch.c` 各有一份 `static`。`StorePrivate.h`、`StoreUiPrivate.h`、`DbPrivate.h` 又各有一份 `static inline` | `CString.c` 没有 `strcmp`。空指针行为不一致：`Device.c` 把空指针当成不相等，`DriverMatch.c` 不检查 |
| `static CopyStr` / `CopyName` | `Device.c`、`TtyUi.c`、`SettingsUiModel.c`、`FilesUi`、`KernelTask.c`、调度器和设备枚举 | 带上限、保证结尾 0。`CString.c` 没有这个。各份对空指针的处理不完全一样 |
| `static Zero` | HDA、EHCI、virtio、学生文件系统，大约 5 处 | 不必新函数。应改成已经存在的 `memset` |
| `HexU64`、`WriteHex2`、`WriteHex4` | Arm/RiscV 交接、`DeviceDump` | 应改调 `Text.h` 的 `FormatHexadecimal`。数字转换已经进过 Library |

不放进 Library 的：`TcpInput`、窗口绘制、`HalVideo*`、驱动匹配、页表。那些是策略或硬件，不是公共函数。

| 序 | PR | 改哪些 | 验收 |
| --- | --- | --- | --- |
| 49 | **PR-LIB-string** | 在 `CString.c` 增加整串相等，以及带上限、保证结尾 0 的拷贝。空指针行为取 `Device.c` 那一版（空来源写成空串，空指针比较结果为不相等）。`ConsoleCmd.c` 的全局 `StrEq` 改为调用它，不再占一个全局单词。`Store` / `Db` 头里的 `static inline` 和各文件的 `static` 拷贝删掉。`DriverMatch` 的调用点在传入前保证非空，比较结果不变 | 三架构 `build.sh`。`check-library-pure` 仍过 |
| 50 | **PR-LIB-hex-rest** | `HexU64`、`WriteHex2`、`WriteHex4` 改为调用 `FormatHexadecimal`。输出字符与现在相同 | 三架构 `build.sh` |

`Zero` 改 `memset` 不单开刀，碰到该文件时顺手换。

## 近程不做、远期要做

下面这些不放进前 18 刀。对照过实现之后，它们是后续阶段，不是永久不做。仍然不照搬 Linux 的 usbcore / mac80211，也不把两个主机控制器并成一个。

- 教学 TCP 留下，给 Shell 的 `tcp listen` / `tcp connect` 用。它已经拆在 `Services/Tcp/` 的几个文件里，但连接状态是 `TcpPrivate.h` 里的一排 `extern`，`NetworkInit.c` 也放在这个目录。远期把它收成一个模块，不删除，也不在 x86 上用开关把它编掉。lwIP 仍是套接字和商店的路径。
- `Include/Core/Debug.h` 无条件包含 `Hal.h`。`DebugWrite` 在默认构建里是空宏。分模块日志已经是 `ToySerialLog.h`。不需要把三种日志宏替换成一种，需要让调试头停止拖进整个 HAL。
- `ClaimInterface` 在 `Drivers/UsbHostController.c` 里直接返回 -1。键盘槽、端点、报告缓冲是 `XHCI/Xhci.c` 的全局量。公共口注释仍写「只认 xHCI，EHCI 是下一刀」，函数 `Current()` 已经会落到 EHCI。
- `FontTtfLoad.c` 能把 `Assets/Fonts/CJK.TTF` 读进内存，注释写明不挂钩取字。课堂上的汉字仍是链进内核的 `cjk32.c`。
- `Core/Driver/Driver.c` 在命中后 `break`。实例数组有 20 槽，绑定仍是一驱动一实例。
- `Process.h` 包含 `Hal.h`，因为 `ProcessExecve` 的参数类型是 `HAL_INTERRUPT_FRAME`。
- 用户路径在 `SyscallFs.c` / `SyscallProc.c` 里按字节调用 `VirtualMemoryCopyFromUser`。
- `Register`、`Control`、`Bulk` 是全局单词。C 的符号是平的，这些名字占着整个内核。命名规范不允许用文件名当前缀，所以远期把它们收进已经存在的操作表，而不是再加一层缩写。

## 刀

每刀三架构 `./build.sh`。碰到桌面绘制的刀再跑 `Runtime/smoke-boot.sh`。

| 序 | PR | 改哪些 | 本刀不碰 | 验收 |
| --- | --- | --- | --- | --- |
| 1 | **PR-STRUCT-early** | `EarlyIdentitySetup` / `Enable` 从 `BringUp/Kernel.c` 挪到 `BringUp/X64/KernelHandoff.c`，在调用 `KernelMain` 之前。`Kernel.c` 去掉 `__x86_64__` 这段 | 模块表顺序、Arm64 / RiscV 交接 | 三架构 `build.sh`。x86 仍在分页初始化前建好 4GiB 恒等映射 |
| 2 | **PR-STRUCT-include** | 驱动私有头改带目录的名字，例如 `Ehci/Private.h`。去掉「每个驱动目录各一条 `-I`、谁在前面谁赢」的搜法。保留 `-I .../Drivers`。现在 EHCI 有 19 处 `#include "Private.h"`，XHCI 已经写成 `XHCI/Internal.h` | 函数体 | 三架构 `build.sh`。不存在靠 include 顺序选中另一个目录的同名头 |
| 3 | **PR-STRUCT-log** | `Core/` 里 16 个包含 `Console.h` 的文件不再调用 `ConsoleWrite`。调度和系统调用的诊断走已经存在的串口日志。`lsdev` 的排版留在 Core，写出函数由调用方传入；Shell 命令传入 `ConsoleWrite` | 提示符、桌面控制台绘制 | 三架构 `build.sh`。`lsdev` 文本与现在一致。Core 不再包含 `Console.h` |
| 4 | **PR-STRUCT-breath** | `SchedulerIoBreath`、`SchedulerWait`、`ProcessLoad` 不再包含 `Gui.h`，也不再直接 `GuiPollMouseMotion` / `GuiCloseAllUserWindows` / `GuiComposeThemeScene`。Gui 在初始化时登记这几个回调；没登记则呼吸只做让出 CPU | 鼠标跟手的时序、关窗的条件 | `build.sh` + `smoke-boot.sh`。长 IO 时指针仍跟着动 |
| 4A | **PR-STRUCT-names** | 只改名字，不改行为。`Services/GuiWm/` 改成 `Services/GuiWindowManager/`，主文件 `GuiWm.c` 改成 `GuiWindowManager.c`。`GuiInit.c`、`GuiHit.c` 留在该目录，函数名不动。`Makefile` 里的目录通配跟着改。`Iwl/IwlPrivate.h` 改成 `Iwl/Private.h`。`XHCI/Internal.h` 改成 `XHCI/Private.h`，包含路径一起改。`Serial/Serial.h` 与 `Ehci/Private.h` 已经符合命名规范 §5，不动。其余仍带目录名的 `*Private.h` 不在这一刀批量改 | 函数体、窗口行为 | 三架构 `build.sh`。公开函数名与改名前相同 |
| 5 | **PR-STRUCT-font-syscall** | `Core/Syscall/SyscallProc.c` 不再包含 `Theme.h`。换字系统调用调 `Library` 的 `FontSetById`，或调 Theme 登记的一个函数。Core 不认识主题 | 字号、主题配色 | 三架构 `build.sh`。系统调用换字之后桌面用的仍是同一套字 |
| 6 | **PR-STRUCT-fs-hooks** | `Services/FileSystem/` 不再包含 `ShellCommands.h` 和 `Store.h`。Shell 初始化时自己登记文件命令。商店用一个「这条路径能不能直接改」的回调挂到文件系统上，没挂则路径不拦截 | 卷的挂载顺序、只读标志 | 三架构 `build.sh`。商店托管文件仍不能被普通删除改掉 |
| 6A | **PR-STRUCT-fs-names** | 只改文件名，不改函数名。主文件留下：`FileSystem/FileSystem.c`、`Console/Console.c`。辅助文件去掉目录名。`FileSystemInit.c` → `Initialize.c`，`FileSystemOps.c` → `Operations.c`，`FileSystemTree.c` → `Tree.c`，`FileSystemMount.c` → `Mount.c`，`FileSystemMountVol.c` → `MountVolume.c`，`FileSystemMountPick.c` → `MountPick.c`。`Install.c` 已经不带前缀，不动。`ConsoleInit.c` → `Initialize.c`。同目录里其余 `Console*.c` 不在这一刀改 | 函数体、命令行为 | 三架构 `build.sh`。公开函数名与改名前相同 |
| 6B | **PR-STRUCT-console-names** | 只改文件名，不改函数名。`Console.c` 留下。其余：`ConsoleAlias.c` → `Alias.c`，`ConsoleBuiltin.c` → `Builtin.c`，`ConsoleCmd.c` → `Command.c`，`ConsoleFocus.c` → `Focus.c`，`ConsoleHist.c` → `History.c`，`ConsoleInput.c` → `Input.c`，`ConsoleLogFile.c` → `LogFile.c`，`ConsoleSbBar.c` → `Scrollbar.c`，`ConsoleSbPaint.c` → `ScrollbackPaint.c`，`ConsoleSbSlot.c` → `ScrollbackSlot.c`，`ConsoleScroll.c` → `Scroll.c`，`ConsoleSerial.c` → `Serial.c`，`ConsoleWrite.c` → `Write.c`。`Initialize.c` 已在 6A 改过 | 函数体、提示符 | 三架构 `build.sh`。公开函数名与改名前相同 |
| 6C | **PR-STRUCT-ops** | 短名统一用登记，不靠加前缀。做法写在命名规范 §6。一个模块对外一张操作表，初始化时填上。实现函数改成 `static`，短名只在本文件。只在本文件里用的函数直接 `static`，不必进表。这一刀只做 `Console/`：`Console.h` 里现在那一排全局函数收成这一张表，调用点改走表。提示符、写出内容和命令登记不变。FileSystem、Gui、Iwl、XHCI 不在这一刀 | 用户敲的命令 | 三架构 `build.sh`。全局符号里不再新增 `Write`、`Initialize`。屏幕上的 Shell 与改前一致 |
| 6D | **PR-STRUCT-fat-names** | 只改文件名，并补回主文件。原来的 `Fat.c` 在拆分时去掉了（`FatPrivate.h` 写着 PR-R3 拆分 `Fat.c`），目录里没有和文件夹同名的文件。这一刀新建 `Fat/Fat.c`，把 `FatInitialize` 从 `FatIo.c` 挪进去。其余去掉 `Fat` 前缀，缩写写全：`FatDir.c` → `Directory.c`，`FatDirName.c` → `DirectoryName.c`，`FatDirScan.c` → `DirectoryScan.c`，`FatDirSlot.c` → `DirectorySlot.c`，`FatDirStress.c` → `DirectoryStress.c`，`FatFile.c` → `File.c`，`FatFileDelete.c` → `FileDelete.c`，`FatFileRead.c` → `FileRead.c`，`FatFileWrite.c` → `FileWrite.c`，`FatFileWriteAt.c` → `FileWriteAt.c`，`FatFileWriteExist.c` → `FileWriteExist.c`，`FatFileWriteNew.c` → `FileWriteNew.c`，`FatFormat.c` → `Format.c`，`FatFsOps.c` → `Operations.c`，`FatIo.c` → `InputOutput.c`，`FatPath.c` → `Path.c`，`FatPathName.c` → `PathName.c`。函数名不动 | 函数体、目录项布局、读写返回码 | 三架构 `build.sh`。公开函数名与改名前相同。`Fat/` 里有 `Fat.c` |
| 7 | **PR-STRUCT-fat** | `Core/FileSystem/Fat/*.c` 移到 `Library/Fat/`。这些文件只包含 `Fat.h`、`FatPrivate.h`、`Block.h`。头已经在 `Include/Library/`。`Makefile` 按 `Library/Gpt` 的方式收 `Library/Fat/*.c`。`Core/FileSystem/Student/` 的内存桩留在 Core，符号不变 | 目录项布局、读写返回码 | 三架构 `build.sh`。现有文件读写测试仍过 |
| 7A | **PR-STRUCT-vfs-names** | `Vfs` 是 Virtual File System 的缩写，写成完整单词。`Include/Library/Vfs.h` → `VirtualFileSystem.h`，`Library/Vfs.c` → `VirtualFileSystem.c`。头里的 `VfsRegister`、`VfsSelect`、`VfsOps`、`VfsMount`、`VfsListDir`、`VfsListEntries`、`VfsReadFile`、`VfsWriteFile`、`VfsDeleteFile`、`VfsMkdir`、`VfsRmdir`、`VfsRename`、`VfsFileStat`、`VfsFileSync`、`VfsReadFileAt`、`VfsWriteFileAt` 以及 `VFS_MAX_BACKENDS` 同样把 `Vfs` 换成 `VirtualFileSystem`，调用点一起改。`FS_OPS` 的字段、`FatFsOps`、`ResFsOps`、`RamFsOps` 不动。`CoreOps.h` 里的 `VfsService*` 是另一层，不在这一刀 | 后端表上的读写行为 | 三架构 `build.sh`。公开的后端函数与改名前一一对应 |
| 7B | **PR-STRUCT-vfs-ops** | `VirtualFileSystem.h` 里那一排全局函数（`Register`、`Select`、`Ops`、`Mount`、`ListDir`、`ListEntries`、`ReadFile`、`WriteFile`、`DeleteFile`、`Mkdir`、`Rmdir`、`Rename`、`FileStat`、`FileSync`、`ReadFileAt`、`WriteFileAt`）收成一张模块表。实现改成 `static`，短名只在 `VirtualFileSystem.c`。这些函数都在这一个文件里，表在本文件填好。调用点改走表。`FS_OPS` 仍是后端插件表，字段不动。`FatFsOps`、`ResFsOps`、`RamFsOps`、`VfsService*` 不动。没有后端时仍返回 `FAT_ERR_IO` | 选中的后端、读写结果 | 三架构 `build.sh`。全局符号里不再新增 `Mount`、`ReadFile`。分发和改前一致 |
| 8 | **PR-STRUCT-hid** | `UsbHid/` 不再包含 `XHCI/Internal.h`（现有 11 个 `.c`）或 `XHCI.h`（`Keyboard.c` 仍包含它，和文件头注释不符）。键鼠只通过 `UsbHostController.h` 和本目录自己的头拿状态。缺的字段补到公共口或 `UsbHid` 私头，不把 xHCI 环结构留在键盘文件里 | 报告格式、灯的 SET_REPORT、中断队列约定 | `build.sh` + `smoke-boot.sh`。QEMU 仍出现键盘和绝对鼠标 |
| 8A | **PR-STRUCT-hid-names** | 只改文件名，不改函数名。`Hid` 是 USB 类的正式名字，留下。`XHCI/HidIntrKbd.c` → `XHCI/HidInterruptKeyboard.c`，`XHCI/HidIntrMouse.c` → `XHCI/HidInterruptMouse.c`。`Intr` 是 Interrupt，`Kbd` 是 Keyboard。文件头里点到旧文件名的注释跟着改。`Makefile` 用目录通配，不用改。`HubEnumKbd.c` 和其他仍带缩写的文件不在这一刀 | 函数体、报告格式、中断队列约定 | 三架构 `build.sh`。公开函数名与改名前相同 |
| 9 | **PR-STRUCT-bot** | CBW、数据、CSW 的公共部分收成一份，xHCI 与 EHCI 都调它，底下走已经存在的 `BulkOn` / `Control`。xHCI 的互斥和 EHCI 的 Clear Halt 留在各自文件 | 扇区命令、sense 的时机、锁的范围 | `build.sh`。QEMU 默认盘是 AHCI，本刀不靠 U 盘冒烟证明容量；用一份与现在字节相同的 CBW/CSW 对照 |
| 10 | **PR-STRUCT-hub** | 对照 `XHCI/Hub.c` 与 `Ehci/Hub.c`。只抽出两边请求字节相同的标准 Hub 请求。队列、门铃、调度留下。若对照后相同部分不到一个小文件，就停，不硬抽 | 把两个主机控制器并成一个 | `build.sh`。抽出的请求与搬之前逐字节相同 |
| 11 | **PR-STRUCT-ip** | `HAL/X64/Drivers/Net/NetRx.c` 与 `HAL/Virt/VirtioNet/VirtioNetRx.c` 里从以太网头拆到 ICMP/UDP/TCP 的那段收成一个函数，两处收包都调它。MMIO 与 PCI virtio 仍是两份驱动 | 删教学 TCP、改 lwIP 的套接字、改 IP 地址 | 三架构 `build.sh`。分发条件与现在两个文件一致，包括「目的地址不是本机就丢」 |
| 11A | **PR-STRUCT-cjk-size** | NUC 实机切中文后笔画发虚（2026-10-18）。原因是 `FontTtfRaster.c` 把汉字先放大再钳进 18×18 灰格。这一刀让汉字按更大的像素栅格画，不再钳回 18。英文仍走原来的点阵。缺字回落、同一行里汉字和 ASCII 各走各的宽度，留给后面的 CJK 刀 | 英文点阵、语言字符串、哪些码点有字 | 三架构 `build.sh`。NUC 上 `lang zh` 笔画比现在的 18 格清楚 |
| 11B | **PR-STRUCT-xhci-files** | `XHCI/` 里控制器和插上的设备平铺在一起。这一刀把 U 盘文件放进 `XHCI/Msc/`，主文件仍是 `Msc.c`，辅助文件去掉 `Msc` 前缀。FTDI 放进 `XHCI/Serial/Ftdi/`，主文件 `Ftdi.c`；CDC 放进 `XHCI/Serial/Cdc/`，主文件 `Cdc.c`。辅助文件去掉芯片前缀。`HidInterruptKeyboard.c`、`HidInterruptMouse.c` 进 `XHCI/Hid/`。函数名不再重复所在目录；和 EHCI 撞名的串口入口收成 `gFtdiUart`、`gCdcUart`，实现改成 `static`。控制器、`Device*`、`Hub.c`、`Enum*`、`Wifi*` 留在 `XHCI/` 根。`Makefile` 为新子目录补通配。U 盘全局变量仍留在 `Xhci.c` | 认领条件、传输、把 xHCI 和 EHCI 并成一个、USB 无线网卡 | 三架构 `build.sh`。认领和传输与搬之前相同 |
| 11C | **PR-STRUCT-cjk-solid** | 11A 实机后笔画仍发虚（2026-10-18）。`GlyphCrispAlpha8` 把 52 到 189 的灰仍半透明混进背景，缩放又按整格字框，墨迹占不满。这一刀按行高的两倍栅格画，屏幕上每个点看背后那四格：够深涂实，不够留空。缩放按字身，不按整格字框。英文仍走原来的点阵 | 英文点阵、语言字符串、哪些码点有字、同一行里的前进宽度 | 三架构 `build.sh`。NUC 上 `lang zh` 笔画是实心的，不再有半透明灰边 |
| 12 | **PR-STRUCT-gui-state** | `Include/Services/GuiPrivate.h` 里大约 58 个 `extern` 收成窗口管理器拥有的一块状态。拖动、光标、焦点通过这一个对象访问。绘制公式不改 | 应用窗口不再看见这块状态（下一刀） | `build.sh` + `smoke-boot.sh`。拖动、缩放、光标跟现在一样 |
| 13 | **PR-STRUCT-gui-apps** | `TtyUiPrivate.h`、`DevicesUiPrivate.h`、`StoreUiPrivate.h` 不再包含 `GuiPrivate.h`。这些窗口只包含 `Gui.h`，需要的窗口矩形通过公开函数拿 | 窗口外观 | `build.sh` + `smoke-boot.sh` |
| 14 | **PR-STRUCT-hal** | 把 `Hal.h` 里的平台、RTC、xHCI 回退地址、PAT、端口、页表、中断按现有 `.c` 拆成多个头。`Hal.h` 仍然包含它们，所以这一刀调用点可以不改 | 删掉 `Hal.h`、改函数语义 | 三架构 `build.sh`。`Hal.h` 的声明集合与拆之前相同 |
| 15 | **PR-STRUCT-paint-desktop** | `Services/Desktop/` 里已经有 `UiFill*` / `UiDraw*` 的实心绘制改走这些函数。`HalVideoPresent`、混合、光标、帧缓冲地址留在 HAL | 其他 Services 目录、特效观感 | `build.sh` + `smoke-boot.sh`。桌面就绪 |
| 16 | **PR-STRUCT-paint-gui** | 同刀 15，范围是 `Services/Gui*` 与 `Services/Ui/`、`Services/Console/` | 设置、文件、商店、设备窗口 | `build.sh` + `smoke-boot.sh` |
| 17 | **PR-STRUCT-paint-apps** | 同刀 15，范围是设置、文件、商店、设备、编辑、TTY 这些窗口 | 新控件 | `build.sh` + `smoke-boot.sh` |
| 18 | **PR-STRUCT-manual** | 用当时的树改写技术手册「分层与对外接口」里和代码不符的段落：没有 `Common/`；FAT 实现在哪；`Elf` 在 `Core/Elf`；几何在 `Library/Gfx`；入口在 `BringUp/Kernel.c`；收包分发在哪 | 产品排期、函数索引大表 | 手册里的目录名能在树里找到 |

## 建议顺序

```text
PR-STRUCT-early
  → PR-STRUCT-include
  → PR-STRUCT-log
  → PR-STRUCT-breath
  → PR-STRUCT-font-syscall
  → PR-STRUCT-fs-hooks
  → PR-STRUCT-ops
  → PR-STRUCT-fat-names
  → PR-STRUCT-fat
  → PR-STRUCT-vfs-names
  → PR-STRUCT-vfs-ops
  → PR-STRUCT-hid
  → PR-STRUCT-hid-names
  → PR-STRUCT-bot
  → PR-STRUCT-hub
  → PR-STRUCT-ip
  → PR-STRUCT-cjk-size
  → PR-STRUCT-xhci-files
  → PR-STRUCT-cjk-solid
  → PR-STRUCT-gui-state
  → PR-STRUCT-gui-apps
  → PR-STRUCT-hal
  → PR-STRUCT-paint-desktop
  → PR-STRUCT-paint-gui
  → PR-STRUCT-paint-apps
  → PR-STRUCT-manual
```

先把依赖方向倒过来的调用点拔掉：控制台、窗口、主题、Shell、商店。再搬 FAT、收 USB 协议和收包分发。窗口的全局状态先收成一块，应用窗口再断开对私有头的包含。画面三刀分开。手册放在最后。

## 还没逐函数看过

上面的刀是抽样，不设 50 的上限。函数所在文件以 `Documents/柱规格/函数表.md` 为准（4923 个定义）。`原则与学习记录.md` 写到 §4.18。自有 `.c` / `.S` 大约 10.6 万行（不含第三方、不含字模）。逐函数写进白皮书的是开机主链，不是这 4923 个函数每个都有一段。

还没逐函数写完的大块，按目录行数：

| 目录 | 行 | 白皮书里已有的 |
| --- | ---: | --- |
| `Drivers/Iwl` | 8778 | §4.9 认领。§4.18 MIC。§4.23 四个入口是 void。§4.34 CCMP/TKIP 校验失败还原密文 |
| `Drivers/XHCI` | 6846 | §4.8 入口。§4.10 键盘队列。§4.26 控制 0/-1，批量失败不复位。MSC 可探第二只 hub |
| `User/` | 6556 | §4.15 演示与 `brk`。§4.24 页面、控件色块、Snake、任务管理器行 |
| `Services/Desktop` | 3685 | §4.13 第一帧。§4.16 网态。§4.25 点击只交动作，槽满画面不动 |
| `Drivers/Ehci` | 3823 | §4.17 调度。§4.19 `EnumDevice` 地址不回退。hub 口复位包装未逐个写 |
| `Core/FileSystem` | 3231 | §4.16 删除。§4.21 写短不放尾簇，改名不搬簇 |
| `Core/Scheduler` | 2938 | §4.23 入队、阻塞、退出。锁不罩等盘。启动与信号未逐个写 |
| `Services/Console` | 2831 | §4.18 登记。§4.22 其余一级命令都在当前任务里跑完 |
| `Services/Store` + `StoreUi` | 4243 | §4.14 相位。§4.21 落盘。§4.40 坏行跳过，成功返回条数 |
| `Drivers/UsbHid` | 2223 | §4.8 Probe 置 `gXhciReady`。§4.10 报告入队与队满丢弃 |
| `Drivers/Video` | 1984 | §4.14 字形。§4.24 选模、EDID、主题、TTF 超过 8MiB 不读 |
| `Drivers/Igpu` + `Hda` | 3161 | §4.13 认卡前返回 0。§4.20 没卡不写 MMIO。Present 提交环未逐个写 |
| `HAL/X64/LwIp` | 1530 | §4.11 轮询。§4.22 `pbuf` 之后进 `ethernet_input`，fd 再进 `ToySocket*`。UDP/ping 未读 |
| `HAL/X64/PageTable` | 538 | §4.10 已写用户槽怎样拆开 |
| `Drivers/Nvme` | 509 | §4.10 已写 `CtrlInit` / `Xfer`。只接受 512 字节扇区 |

§4.40 已写：商店目录坏行只跳过，成功返回条数。点名的缺口写到这里。字模和第三方 lwIP 不逐字节看。函数表里没单独成段的辅助函数，短注不是读完函数体。不把一个目录收成一刀。

逐函数的看法：沿 §4.0 主线，一个函数一段。记它的输入、输出、改了哪块状态、为什么这样写。不逐行抄语句。数据表按生成脚本和查找函数看，不逐个字节看。某一段讲清楚了，才把「为什么」写回该段源文件。

所以刀数不是心里那个改动量。心里的量是：主线上每个函数都能讲清楚，注释写回代码，这段里欠的债顺手还。结构刀只是其中碰到边界时的那一笔。碰到一处就拆一刀，50 不够就继续编号。

## 第 51 刀起

下面这些已经对过代码，还没有编号。每刀只改一类，三架构要能编。还没读完的目录不在这张表里占位。

| 序 | PR | 改哪些 | 验收 |
| --- | --- | --- | --- |
| 51 | **PR-XHCI-extern-cdc** | `CdcTx.c` / `CdcRx.c` 里那十几行 `extern` 收进 CDC 自己的头，由 `CdcClaim.c` 定义。`Internal.h` 里已有的符号不再写第二遍 | `build.sh`。符号还是原来那些全局量 |
| 52 | **PR-XHCI-extern-ftdi** | 同刀 51，范围是 `FtdiRx.c`、`FtdiTx.c`、`FtdiHub.c` | `build.sh` |
| 53 | **PR-XHCI-extern-wifi** | 同刀 51，范围是 `WifiHub.c` 从 `WifiClaim.c` 拉来的路由和 Hub 槽 | `build.sh` |
| 54 | **PR-ABI-damage** | `TOY_GFX_DAMAGE_RECT` 放到 `Include/Abi`。用户头和 `SyscallProc.c` 都包含它。`DAMAGE_RECT_DESC_SIZE` 改成对该结构的 `_Static_assert`，仍是 24 字节 | 三架构 `build.sh`。贴像素的布局与现在相同 |
| 55 | **PR-ABI-tasksnap** | 内核 `TASK_SNAP` 与用户 `TOY_TASK_SNAP` 收成 `Include/Abi` 里的一份。字段顺序和宽度不变 | 三架构 `build.sh`。`list tasks` 的输出字段不变 |
| 56 | **PR-GUI-user-slot** | `GUI_WINDOW` 里用户按钮、点击、键队列拆到旁边的用户窗记录，用同一个下标。拖动和终端行留在原结构。放在刀 12 之后 | `smoke-boot.sh`。开窗、按钮、按键与现在一样 |
| 57 | **PR-ENG-ci-path** | `Kernel/.github/workflows` 里的 `Build/CodeA-HAL` 改成 `Build/HAL/<Arch>/Kernel.elf`。workflow 仍放在 `Kernel/.github`，这一刀不讨论仓根会不会跑它 | 对照 `Kernel/Makefile` 的 `HALDIR`。不在这刀跑 GitHub |
| 58 | **PR-ENG-syscall-check** | 加一个脚本：`SyscallABI.h` 的 `SYS_*` 号和 `SyscallDispatch` 的 `case`、用户态 `syscall*.S` 里的号一致。接进 `make`，对不上就失败 | 三架构现有号通过。故意改一个号时脚本失败 |
| 59 | **PR-COMMENT-block-order** | `HalDevices.c` 的注释改成和调用顺序一致：先 AHCI，再 ATA，再 NVMe。不改登记顺序 | 只改注释。Bind 结果不变 |
| 60 | **PR-ELF-boot-kernel** | Boot 的 `ELF_HEADER_64` 与内核 `Elf.h` 的程序头字段做编译期或脚本对照（魔数、`PT_LOAD`、64 位小端）。两份仍各留在自己的树里，Boot 不包含内核头 | 字段偏移不一致时检查失败。装核行为不变 |
| 61 | **PR-KBD-queue-lock** | 完成路径上的 `KbdPush` 与 `DequeueKeyboard` 用同一把 `gHidQueueLock`。队满仍丢新报告，不改成覆盖旧键 | `build.sh` + `smoke-boot.sh`。QEMU 键盘仍能打字 |
| 62 | **PR-COMMENT-lies** | 只改和函数体不一致的文件头：`Keyboard.c` 仍包含 `XHCI.h`；`Font.h` 的字模路径，以及「汉字默认 16×16」改成 18×18；`XHCI.h` 默认是轮询不是 MSI-X；四份网卡 Bind 写的是 `NetAttachNicKind` 不是 `NetAttachNic`；`Desktop.h` 与 `Desktop.c` 的 `Gui.h`；`HdaMmio.c` 会写 MMIO；`IwlEapol.c` 写 GTK 从略，函数会 `IwlInstallGtk` | 只改注释。行为不变 |

注释欠账不单开「给全部文件补注释」的一刀。某一函数讲清楚了，才把「为什么」写回该函数。没讲清楚的不补套话。

## 第 63 刀起

下面这些是 2026-10-17 把开机主链读完之后对上的，每刀只改一类。成功路径保持现在的返回码。删文件不放簇、返回码 0 和 1 混用、列表只画色条、槽满双击画面不动，不在这里编号。

| 序 | PR | 改哪些 | 验收 |
| --- | --- | --- | --- |
| 63 | **PR-EHCI-addr** | `EnumDevice`（`Ehci/EnumDevice.c`）在 `EnumSetAddr` 成功之后才把 `gEhciNextAddr` 加上。SET_ADDRESS 失败时号不动，返回 0。地址已经被设备接受、后面的配置或找 HID 失败时，复位这个根口，让设备回到地址 0，不置 `HidOk`。boot 协议和空闲请求失败仍忽略，这一刀不改 | 三架构 `build.sh`。成功的一只键盘仍返回 1。失败之后下一只设备不会撞上没还回去的号 |
| 64 | **PR-EHCI-sched-stop** | `SchedStart`（`Ehci/Sched.c`）在已经写过 Run 或 ASE 之后失败，先停跑再返回 0。`Sched` 保持 0，`Up` 改成 0。还没写命令寄存器就失败的，只返回 0，不动 `Up`。`SchedEnablePeriodic` 等不到周期状态位时，把刚写上的周期使能清掉 | `build.sh`。成功路径仍返回 1 并置 `Sched`。失败之后这只控制器不再被当成已调度 |
| 65 | **PR-DISP-after-probe** | `InitializeVideo` 里那次核显和声卡初始化留着：没探到卡就返回 0。`InitializeDriver` 认卡之后再调一次同一组入口（MMIO、forcewake、GTT、blit、HDA 的 CORB 和流）。这一刀不把桌面从 GOP 帧缓冲换成核显送屏 | 三架构 `build.sh`。没有核显或声卡的 QEMU 仍返回 0，桌面仍起来 |
| 66 | **PR-XHCI-evt-drain** | 独占窗口时 `DrainEvents`（`XHCI/Irq.c`）仍要跑 `ProcessEvents`，把 ERDP 推进去，环上的槽空出来。键鼠报告可以晚一点再交给窗口。不改 `EVT_SIZE`（现在是 128） | `build.sh` + `smoke-boot.sh`。QEMU 键盘仍能打字。独占期间事件环不会因为没人取而写满 |
| 67 | **PR-XHCI-bulk-reset** | `XhciBulkXfer`（`XHCI/MscBulk.c`）在门铃之后超时或完成码不是成功短包（1 或 13）时，复位这只批量端点并把取环指针对齐，再返回 -1。`gMscClaimed` 不动。参数不合法、还没敲门铃的失败不复位。控制传输仍走现有的 `RecoverEp0` | `build.sh`。成功的扇区读写返回值不变。失败之后下一次批量不是接着用上次留下的 TRB |
| 68 | **PR-SIG-retry** | `SchedulerOnTimer`（`SchedulerSignal.c`）在处理函数写帧成功或终止成功之后才把 `PendingKill` 清成 0。写帧失败时信号留着，下一拍再试。x64 的 `HalFrameSignalSetup` 在用户栈没写上之前不提交新的指令指针和第一个参数；失败时帧回到调用前。`SIGKILL` 的退出码仍是 128 加信号号 | 三架构 `build.sh`。忽略的处理函数仍直接清掉信号。写帧失败后任务仍从原来的指令继续 |
| 69 | **PR-VM-fail-unmap** | `ElfLoadFromMemory` 中途失败时，卸掉这一趟已经映上的页并释放物理页。`VirtualMemoryMapRange` 同样，只卸这一趟的页。Arm64 和 RiscV 的 `HalPageMap` 在挂上新的空中间表之后失败，把这张空表摘掉并释放；已经存在的叶子不动。地址空间 128 页的上限不改 | 三架构 `build.sh`。成功的装载和映射与现在相同。失败之后这次尝试的用户页表项不留下 |
| 70 | **PR-UDP-port** | `LwIpUdpSend`（`HAL/X64/LwIp/toy_udp.c`）在还没有监听连接块时，绑定端口传 0，让 lwIP 自己选临时端口，不再写死 40000。教学栈 `UdpSend` 没绑定时仍用 40000。两套栈仍不合并，也不查对方的端口表 | 三架构 `build.sh`。lwIP 关掉时教学 `udp send` 源端口仍是 40000 |
| 71 | **PR-PMM-reserve-ref** | `PaintRange`（`PhysicalMemorySeg.c`）把保留页涂进位图时不写引用计数，所以计数是 0。`PhysicalMemoryFreePages` 见到位图已占用且计数是 0 或 1 就会清位。保留范围（第 0 页、`0x7000` 起的 8KiB、内核映像）涂位时加上保留标记。`FreePages` 见到这个标记就返回，不改位图、不改计数。普通分配仍从计数 1 开始。越过 4GiB 整段跳过的规则不改 | 三架构 `build.sh`。对保留页调用释放之后，该页仍不能被 `AllocatePages` 分出去 |

## 远期

近程 18 刀做完再开。每一刀仍然只改一类行为，三架构要能编。

### 日志和门面

| 序 | PR | 改哪些 | 验收 |
| --- | --- | --- | --- |
| 19 | **PR-FAR-debug-hal** | `Debug.h` 不再包含 `Hal.h`。打开调试时，`DebugWrite` 调一个在 Core 里的函数，由那个函数再进串口。默认关闭时仍是空宏 | 默认 `build.sh` 行为不变。`DEBUG=1` 时调试串仍出现 |
| 20 | **PR-FAR-frame** | `ProcessExecve` 不再在 `Process.h` 里暴露 `HAL_INTERRUPT_FRAME`。头文件用一个不完整类型或 `void *`，实现文件再包含架构帧 | Core 的公开头不再包含 `Hal.h` |
| 21 | **PR-FAR-hal-core** | Core 里只需要内存或中断的文件，从 `Hal.h` 改成刀 14 切开的窄头。一次一个目录：先 `PhysicalMemory`，再 `VirtualMemory`，再 `Scheduler` | 每目录一刀提交。三架构 `build.sh` |
| 22 | **PR-FAR-hal-services** | 同刀 21，范围是不再画像素的 Services：`FileSystem`、`Store`、`Tcp`、`LwIp` | 桌面绘制留在刀 15–17 的结果上 |
| 23 | **PR-FAR-hal-drop** | 没有任何 `.c` 再包含总头 `Hal.h` 之后，总头改成只转包含窄头的兼容文件，或删掉。以当时的包含统计为准，没有清零就不删 | `rg '#include \"Hal.h\"'` 只剩兼容说明或为零 |

### 用户边界

| 序 | PR | 改哪些 | 验收 |
| --- | --- | --- | --- |
| 24 | **PR-FAR-usercopy** | 路径拷贝改成一次最多 `PATH_MAX` 字节，失败与现在一样返回错误。去掉按单字节循环 | 过长路径仍失败。现有打开、执行测试仍过 |
| 25 | **PR-FAR-usercopy-rest** | 其余 `VirtualMemoryCopyFromUser` 的调用点核对长度上限。没有上限的补上，调用方已有上限的不改 | 用户缓冲区越界不写进内核 |

### USB 总线

主机控制器仍是 xHCI、EHCI、UHCI 三份传输。不合成一个，也不先搬到别的架构。先让设备和接口从控制器全局量里出来。

| 序 | PR | 改哪些 | 验收 |
| --- | --- | --- | --- |
| 26 | **PR-FAR-usb-comment** | `UsbHostController.c` 的文件头改成和 `Current()` 一致：有 xHCI 用 xHCI，否则 EHCI | 只改注释 |
| 27 | **PR-FAR-usb-device** | 增加 USB 设备与接口对象：地址、接口类、端点。xHCI 枚举填这张表。键盘全局先留在原处，表只作记录 | `smoke-boot.sh`。QEMU 键鼠仍在 |
| 28 | **PR-FAR-usb-kbd-state** | `Xhci.c` 里键盘槽、端点、报告缓冲移到 `UsbHid`。控制器文件只留环、门铃、端口 | 键盘灯和中断队列与现在一致 |
| 29 | **PR-FAR-usb-claim** | `ClaimInterface` 按类在设备表里认领，返回管道。键盘不再靠「空管道就是键盘」这条约定。鼠标同样用管道 | `smoke-boot.sh`。空管道调用改为显式管道，行为不变 |
| 30 | **PR-FAR-usb-uhci** | UHCI 用同一套 `Register`。`Current()` 的选择写明优先级，不在这一刀改枚举 | 无 UHCI 时启动与现在相同 |
| 31 | **PR-FAR-usb-pci** | xHCI 里读 PCI 配置、取 BAR 的代码留在探测文件。环和传输文件不再包含 `PCIe.h` | 三架构 `build.sh`。这刀做完才谈得到把传输层放到别的总线上，现在不搬架构 |

### 网络

教学 TCP 留下。Shell 在 lwIP 没有接过回显时仍调 `TcpListen` / `TcpConnect`。lwIP 继续负责套接字。两套收包的开关 `gLwIpRx` 保持现在的意思。

| 序 | PR | 改哪些 | 验收 |
| --- | --- | --- | --- |
| 32 | **PR-FAR-tcp-state** | `TcpPrivate.h` 里那十几个 `extern`（`gState`、序号、发送缓冲、接收缓冲）收成一块 `TCP_CONNECTION`，由 `Tcp.c` 持有。`TcpInput*.c` 和 `TcpSend.c` 通过这块状态读写。`Tcp.h` 的 `TcpListen`、`TcpConnect`、`TcpSend`、`TcpRecv`、`TcpInput` 不变 | 三架构 `build.sh`。`tcp listen` 与 `tcp connect` 的状态切换与现在相同 |
| 32A | **PR-FAR-tcp-names** | 只改名字，不改握手和收发。主文件 `Tcp.c` 留下。辅助文件去掉 `Tcp` 前缀，缩写写全：`TcpPrivate.h` → `Private.h`，`TcpInput.c` → `Input.c`，`TcpInputSyn.c` → `InputSynchronize.c`，`TcpInputEst.c` → `InputEstablished.c`，`TcpSend.c` → `Send.c`。同目录包含 `Private.h`。`NetworkInit.c` 留给刀 34。目录里互相调用的函数去掉 `Tcp` 前缀，名词在动词前，并且不重复所在文件名：`TcpSendSegment` → `SegmentSend`，`TcpArmRetrans` → `RetransmitArm`，`TcpFlushSend` → `BufferFlush`，`TcpQueueBytes` → `BytesQueue`，`TcpProcessAck` → `AckProcess`，`TcpRetransmit` → `Retransmit`，`TcpInputListen` → `Listen`，`TcpInputSynSent` → `SynchronizeSent`，`TcpInputSynRcvd` → `SynchronizeReceived`，`TcpInputEstablished` → `Established`。只在本文件用的 `static` 同样去掉 `Tcp` 前缀。对外的 `Tcp.h` 仍带模块名 `Tcp`，避免和别的 `Listen`、`Send` 撞车；动词放在名词前面的改过来：`TcpGetState` → `TcpStateGet`，`TcpGetWindowStats` → `TcpWindowStatsGet`，`TcpSendAck` → `TcpAckSend`，`TcpInput` → `TcpSegmentInput`。`TcpInitialize`、`TcpListen`、`TcpListenStop`、`TcpConnect`、`TcpSend`、`TcpClose`、`TcpRecv`、`TcpPoll`、`TcpLocalPort`、`TcpPeerIp`、`TcpPeerPort`、`TcpPeerClosed` 不动。调用点跟着改。`LwIpTcp*` 不动 | 三架构 `build.sh`。`tcp listen` 与 `tcp connect` 的状态切换与现在相同 |
| 33 | **PR-FAR-tcp-console** | `InputSynchronize.c`、`InputEstablished.c`（刀 32A 改名之后）不再包含 `Console.h`，不再直接 `ConsoleWrite`。失败文字交给 Shell 已经在用的路径，或交给一个由 Shell 登记的写函数 | Shell 里 `tcp:` 开头的失败句仍在 |
| 34 | **PR-FAR-tcp-folder** | `NetworkInit.c` 移出 `Services/Tcp/`。`InitializeNetwork` 仍先 `HalNetInitialize`，再 `UdpInitialize` 和 `TcpInitialize`。教学栈目录里只留连接状态机。`Tcp.h` 的说明改成：这是单连接演示栈，和 lwIP 并存，lwIP 打开时收包仍走 `gLwIpRx` | 三架构 `build.sh`。模块表里的 Network 一项仍在 |

### 多实例

| 序 | PR | 改哪些 | 验收 |
| --- | --- | --- | --- |
| 35 | **PR-FAR-instance-e1000** | 去掉 `Driver.c` 里「命中即 break」只对这一把刀指定的驱动生效，或改成该驱动自己声明能要几个实例。先做 E1000：两张相同的卡能得到两个实例。其它驱动仍是一个 | QEMU 只有一张网卡时，行为与现在相同 |
| 36 | **PR-FAR-instance-block** | AHCI 或 NVMe 按同一规则。现在 `Nvme` 只初始化一个控制器，这刀再允许表里的下一块 | 单盘 QEMU 仍挂上原来的盘 |

### 字

| 序 | PR | 改哪些 | 验收 |
| --- | --- | --- | --- |
| 37 | **PR-FAR-glyph-file** | `FontTtfLoad` 或已有的 TOYF 运行时槽接到取字。缺字时仍回落到链进内核的点阵 | 桌面汉字仍在。文件字加载失败不挡启动 |
| 38 | **PR-FAR-glyph-unlink** | 课堂用到的汉字都由文件提供之后，才停止把 `cjk32.c` 链进内核。现在没有这份文件：Arm64 / RiscV 的汉字只靠点阵，所以这一刀不摘链。点阵文件留在仓库里 | 三架构 `build.sh`。内核仍链着 `cjk32.c` |

### 窗口

不重写。刀 12、13 把状态收拢之后再做。

| 序 | PR | 改哪些 | 验收 |
| --- | --- | --- | --- |
| 39 | **PR-FAR-damage** | 合成只重画脏矩形。拖动备份可以留下，但整屏刷新不再是唯一路径 | `smoke-boot.sh`。拖动窗口时其余窗口不闪 |
| 40 | **PR-FAR-input-owner** | 指针和键盘有一个当前拥有者：焦点窗口或拖动。`GuiPointer` 不再直接改每个窗口的私有状态 | 点击、拖动、关闭与现在一致 |

### 命名

不单开一轮全库改名。某个模块已经按上面的刀打开时，顺手把该模块的公共符号收成全词。单独值得一刀的只有现在这种过短的全局名。

| 序 | PR | 改哪些 | 验收 |
| --- | --- | --- | --- |
| 41 | **PR-FAR-usb-names** | `Register`、`Control`、`Bulk`、`InterruptQueue` 不再作为全局符号。`Xhci.c` 里的全局 `Init` 一并收进同一张操作表。调用方持有 `USB_HOST_CONTROLLER_OPERATIONS`，或只通过已有的 `BulkOn` 那种带控制器种类的入口。函数名仍不重复文件名 | 三架构 `build.sh`。键鼠和 U 盘调用点改完后符号表里没有这些单词 |

### 临界区

| 序 | PR | 改哪些 | 验收 |
| --- | --- | --- | --- |
| 42 | **PR-FAR-irq-hold** | 列出 `HalIrqSave` 之后还调用 Gui、磁盘或 USB 等待的地方。刀 4 清掉调度器里那一处。其余每处单独一刀：持有关中断的段落只做内存读写，等待改到开中断之后 | 每刀注明改的是哪一个函数。桌面和磁盘冒烟仍过 |

远期顺序：19 → 25 可以紧跟近程手册之后。USB 26 → 31 依赖近程的 HID 和 BOT。网络 32 → 34 依赖近程的收包合成，并且不删教学栈。多实例、字、窗口、命名、临界区彼此独立，可在 USB 和网络之后并行排，但一次只做一把刀。语言和汉字（43 → 48）不挡主线复习，可以在桌面第一帧看过 `FontGlyphCp` 之后做。

## 汉字与语言

汉字现在有两条路，在 `FontGlyphCp`（`Services/Fonts/FontRegistry.c`）：

- ASCII 用当前点阵（Terminus）。
- 其余码点先查 TTF 缓存。缓存没有就放进等待队列，这一帧改走 `FontCjk32Lookup`。
- `cjk32.c` 是 GB2312，7445 字，18×18、4bpp，链进内核。不在这张表里、TTF 又还没栅格出来时，`VideoDrawCodepointAt` 画缺字框。
- TTF 栅格只在 x86 上编译，格子固定 18。Arm64 / RiscV 的 `FontTtfRasterCp` 直接失败，汉字只靠点阵。
- 预热只扫中文目录 `gZh`。

语言是两套平行数组。`LOC_LANG` 只有英文和中文。`LocStr` 在 `gZh` 和 `gEn` 里二选一。`LocaleSet` 拒绝这两个以外的值。设置页和 `lang` 命令把列表写死成两项。`Assets/Locale/en.txt` 和 `zh.txt` 已经是 `MSG_键=译文`，装载却只认这两个文件名。加一种语言现在要改枚举、再写一份 fallback 的 `.c`、再改设置和命令。

要改成：新语言是多一个文本文件，不改 C 的分支。

| 序 | PR | 改哪些 | 验收 |
| --- | --- | --- | --- |
| 43 | **PR-LANG-registry** | 语言改成一张表：代码（如 `en`、`zh`）、当前表、内建回落。`LocStr` 查当前表，缺键用英文内建。`LocaleSet` 接受表里有的代码。`en` 与 `zh` 仍内建，没有文件也能开机 | 三架构 `build.sh`。`lang en` / `lang zh` 与现在一样 |
| 44 | **PR-LANG-file** | 启动时读 `Assets/Locale/<代码>.txt`。键必须是现有 `MSG_*`。多出来的文件就成为可选语言，不必再写 `LocaleTableXx.c`。没有对应文件的代码不出现在列表里 | 放一份试验用的语言文件后，`lang` 能切过去。缺的键显示英文 |
| 45 | **PR-LANG-ui** | 设置里的语言列表和 `lang` 的用法从注册表生成，不再写死「1 英文 2 中文」。`DevicesUiSummary.c` 里按 `LOC_LANG_ZH` 分支的句子改成文案键 | `smoke-boot.sh`。设置里能看见表里的每一种语言 |
| 46 | **PR-CJK-cell** | 同一行里的 ASCII 和汉字按各自宽度前进，不把 16×32 的拉丁字母和 18×18 的汉字塞进同一个格子。缺字框保留 | `smoke-boot.sh`。桌面中文标签不重叠、不裁掉 |
| 47 | **PR-CJK-miss** | 不在 GB2312 里的码点，x86 上等 TTF 栅格完成后再画。等待队列满了仍回落到点阵或框。Arm64 / RiscV 没有 TTF，继续只用点阵 | x86 桌面。GB2312 以外的一个汉字在预热之后能显示 |
| 48 | **PR-CJK-preheat** | 预热当前语言的界面字符串，不只预热 `gZh`。切语言时为该语言里的非 ASCII 排队 | 切到带非拉丁字母的语言后，标签能出来 |

刀 37、38（文件字接上取字、再停止链接 `cjk32.c`）仍放在这两刀之后。点阵在 TTF 不可用时还要留着。

## 写代码时常碰见的问题

这一节按可读性、模块化、性能、命名、工程化来记。只记打开文件能核对的。不把系统改成 Linux，也不为了行数把每个文件再拆一遍。命名、热路径、死代码和刀 1–50 没覆盖的边界还在对源码，对完补在下面。

### 可读性：已经超过 300 行的自有实现

不含 `ThirdParty/`、不含 `cjk32.c`、不含两份 Terminus 点阵。下面这些 `.c` 已经不少于 300 行。约定是新文件不超过 300 行，不是把现有的按行数切开。

| 行 | 文件 |
| ---: | --- |
| 620 | `Kernel/HAL/X64/LwIp/toy_socket.c` |
| 388 | `Boot/BootPkg/LoadKernel.c` |
| 380 | `Kernel/Services/Store/StoreQuery.c` |
| 372 | `Kernel/Services/SettingsUi/SettingsUiPaint.c` |
| 364 | `Kernel/HAL/X64/Drivers/Video/VideoGlyph.c` |
| 334 | `Kernel/HAL/X64/Drivers/Msc/UsbMsc.c` |
| 334 | `Kernel/Core/Syscall/SyscallProc.c` |
| 312 | `Kernel/Services/DevicesUi/DevicesUiSummary.c` |
| 310 | `Kernel/Services/FilesUi/FilesUi.c` |
| 307 | `Kernel/Services/StoreUi/StoreUiModel.c` |
| 300 | `Kernel/Services/GuiUser/GuiUser.c` |
| 300 | `Kernel/HAL/X64/Drivers/Hda/HdaCodec.c` |
| 300 | `Kernel/HAL/X64/Drivers/Ehci/MscClaim.c` |

头文件里更长的是 `XHCI/Internal.h`（535）、`IwlRegs.h`（353）、`GuiPrivate.h`（308）。后一个已经是刀 12。

### 模块化：刀 1–50 没写过的

`Services/` 没有直接包含 `XHCI/Internal.h`、`Ehci/Private.h` 或 iwl 私头。GPT、FAT 各一份实现。TCP 仍是教学栈加 lwIP，没有第三份。

还没写进刀里的：

- **xHCI 的 `.c` 在头文件之外 `extern` 别的文件的全局。** `CdcTx.c` 12 行、`FtdiRx.c` 8 行、`WifiHub.c` 和 `FtdiTx.c` 各 6 行，端点、路由、环从 `CdcClaim.c` / `FtdiClaim.c` / `WifiClaim.c` 直接拉过来。`Internal.h` 里已有的符号，这些文件又写了一遍。链接器符号（`__kernel_end`、链进内核的 `hello`）不是这类问题。
- **ELF 装载有第二份。** `Boot/BootPkg/LoadKernel.c` 自己定义 `ELF_HEADER_64`，按 `PT_LOAD` 装内核。内核侧是 `Include/Core/Elf.h` 和 `Core/Elf/`。Boot 不能包含内核头，两份字段必须继续对照，不能收成一个文件。
- **用户窗和窗口管理器的状态在同一个 `GUI_WINDOW` 里。** 拖动、终端行、用户按钮、点击坐标、键队列都在 `GuiPrivate.h` 这一个结构。刀 12 收的是外面那排 `extern`，没有把用户窗字段拆出去。
- **贴像素的描述符写了两遍。** 用户态 `ToyGfx.h` 的 `TOY_GFX_DAMAGE_RECT` 是 `X,Y,W,H,Pixels*`。内核 `SyscallProc.c` 用固定的 24 字节去拆，没有包含用户那个结构。任务快照同样是内核 `TASK_SNAP` 和用户 `TOY_TASK_SNAP` 各一份。`SyscallABI.h` 本身只有号和常量，没有把内核私有结构放进去。
- **单例。** 块设备是 `Library/Block.c` 的 `gBackend`。网卡是 `DriverNet.c` 的 `gNetBackend`，后挂上的盖掉前一个。声音只有 HDA，`HalAudioPlayPcm` 固定进 `HdaAudioPlayPcm`，当前节点是 `Hda.c` 里的 `static gHdaNode`。画面的前缓冲是 `Video.c` 的 `gFront`。

### 工程化：构建现在守住什么

默认 `make` 只强制一件事：`check-library-pure`。`Library/` 不能包含 `Hal.h`、内存、文件系统、设备、调试、调度、进程、控制台、主题、`Gui.h`，也不能调用 `Hal*`。命中就 `exit 1`，不链内核。机器上没有 `rg` 时，这条 `if rg` 的条件为假，配方仍然成功，等于没查。

`CFLAGS_BASE` 是 `-Wall -Wextra`，没有 `-Werror`。告警打出来，构建照过。唯一关掉的是 x86 `FontTtfRaster.c` 的 `-Wno-unused-parameter`。

宿主测试的配方在 Makefile 里：`runtests`（调度）、`runtests-memory`、`runtests-fs`。它们不进默认 `all`。依赖的 `Kernel/Tools/Tests/`（`TestScheduler.c`、`TestMemory.c`、`TestFs.c`）当前树里没有，`make runtests` 会在编译阶段找不到源文件。

仓库根没有 `.github/workflows`。写好的 workflow 在 `Kernel/.github/workflows/build.yml` 和 `release.yml`，GitHub 不会跑它们。这两份还在检查 `Build/CodeA-HAL/.../Kernel.elf`。现在的产物是 `Build/HAL/<Arch>/Kernel.elf`（`Kernel/Makefile` 里 `HALDIR`）。即使把 workflow 挪到仓根，三架构编译成功之后，检查仍会因为路径对不上而失败。

QEMU 冒烟是另外几份脚本，不在默认构建里。`Runtime/smoke-boot.sh` 看桌面就绪。`Kernel/OpenBox/Scripts/lib/` 里还有 shell、用户态、文件系统的 expect，以及 arm/riscv 的 virt 冒烟。`check-smells.sh` 要找 `Common/` 目录，这棵树没有，一跑就 `exit 2`。

规范里写了、构建没有守住的：警告当错误、系统调用号和分发表对账、返回值必须处理或写成 `(void)`、新 `.c` 不超过 300 行、Core/Services 不去包含驱动私有头。

### 性能：会反复发生的

调度锁没有跨过磁盘或 USB 等待。扇区读写和 USB 传输靠忙等占住当前 CPU。

- xHCI 的 `gMscBotBusy` 从抢到锁一直盖住整段 CBW、数据和 CSW（`MscBot.c`）。热拔插探测走同一把锁。等待期间另一核空转。
- `EventEnterExclusive` 在门铃到完成之间一直保持。这段里 USB 中断被丢掉，键鼠完成要等这笔传输结束。
- 磁盘忙等：ATA 每扇区最多各 100 万次等忙和等 DRQ；AHCI 每段先等 100 万次再等 CI 200 万次；NVMe 完成队列保底 200 万次；EHCI 的 `WaitQtd` 最多 800 万次。
- 进桌面时 `FontTtfLoad` 把 `CJK.TTF` 整文件读入，上限 8MiB。壁纸最多再读 512KiB，4K 屏缩放要第二块约 32MB 的缓冲。这些和点阵、语言包串在同一次 `InitializeGui` 上，共用 FAT 的全局簇缓冲。
- 目录按名查找从簇链头扫到匹配。建项时定位槽位会按项数乘簇链长度反复读 FAT。
- 上屏每条最多 64 行在关中断里拷贝。`GuiPaintWindow` 把画窗和 `Present` 包在更外层的关中断里，条带之间的恢复盖不住这层。`InputTask` 在关中断下扫 xHCI 事件环。刀 42 已经要求持有关中断的段落不再做等待；这两处是它的具体落点。

系统调用逐字节拷贝仍在，不另开刀。返回值不在这一节改。

### 返回码和没有调用点的声明

同一层两套成功码，不收成一刀去改数字，改了调用方会对反。

- 驱动 Probe、模块 `Init`、FAT 公开 API：成功是 0。
- xHCI `Init`、NVMe `NvmeSetup`、ATA 读写、`XhciMscReadSectors`、`BlockFlush` 在没有回调时：成功是 1。
- 描述符 `GetDeviceDesc` 成功是 0，`ConfigureIntr` 成功是 1。见 §4.10。

头文件里声明了、全仓库没有调用点的，包括 `AbandonNoHid`、`AtaInit`、`ClaimInterface`（直接返回 -1）、`NetAttachNic`（Bind 走的是 `NetAttachNicKind`）、空的 `E1000ApplyI219Tarc`，以及 `HdaPciDid`、`IgpuPciDid`、`IwlPciDid`、`WifiGetMac`、`FatListRoot`、`HalAudioStop`、`DeviceUnbind`、`GptFindFatStart`、`EarlyIdentityRoot`。删声明要单独看是不是留给学生或下一刀的口，不在这一刀批量删。

### 命名：扫过，不按计数改名

对照 `Documents/规范/命名规范.md` 扫了 `Kernel/`、`Boot/BootPkg/`、`Kernel/User/`。跳过第三方、EDK2、Build、`cjk32.c`。POSIX 和 CRT 的小写（`memcpy`、`main`、`socket`）以及正式协议名没有算进去。`Hal` 目录名、`XhciBaseAddress`、`Gfx` 前缀、已经留下的 `Xhci` / `Ehci` 函数前缀也不算违例。

扫出来的数量：函数名重复文件名大约 1440 个；辅助文件重复文件夹名大约 387 个，分布在大约 71 个目录；全局函数里的简写大约 171 个；参数和局部变量里的 `Len`、`Buf`、`Dst` 大约 1900 处。

这些数不变成改名计划。

- 1440 个「函数名带文件名」是旧代码的常态，例如 `HalInitialize`、`SchedulerInitialize`。新代码按规范写。不单开一柱把它们改短。
- 387 个辅助文件的重复前缀，碰到那个目录时再去掉。`XHCI/`、`Ehci/` 的辅助文件已经不带控制器前缀。不按目录拆 71 刀。
- 刀 2 之后，包含路径上还有三种头文件名字：`Ehci/Private.h`、`Iwl/IwlPrivate.h`、`Serial/Serial.h`。前两种是辅助头，后一种是主文件。统一成：主文件 `目录/目录.h`，辅助头 `目录/Private.h`（`XHCI/Internal.h` 用同一个词）。见命名规范 §5。这是 4A，只改已经点名的这几个文件。后面的刀号不顺延。
- 用户态包装 `toy_syscall`、`toy_write` 这一族留小写。它们是给用户程序看的调用面，改成 PascalCase 会换掉教学接口。
- 全局单词 `Register`、`Control`、`Bulk` 在 `UsbHostController.c`，`Init` 在 `Xhci.c`。前三个是刀 41。`Init` 收到同一张操作表时一起改，不另编号。
- 五处 `static Zero` 和一处 `static Copy` 仍按前面的说法，碰到文件时改成 `memset` / `memcpy`，不单开刀。
- `toy_udp.c` 这类 lwIP 包装文件名保持现状。lwIP 上游符号不改。
