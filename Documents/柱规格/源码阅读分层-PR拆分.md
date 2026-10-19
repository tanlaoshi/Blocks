# 源码按阅读分层 — PR 拆分

> 状态：第 3 刀已完成。★ 在第 4 刀。
>
> 刀 1 到 71 收的是文件名、主文件和头文件。目录仍按历史长出来，读的时候要在 `HAL` 里钻很深，`Services` 里又是一排平级的窗口程序。
>
> 这一柱只搬家，让从 `BringUp` 模块表出发时，每一层文件夹名能回答下一份该打开谁。不把子目录个数和行数裁成一样。不改协议行为。不合并 xHCI 与 EHCI，不合并教学 TCP 与 lwIP。

## 不在这柱里

| 位置 | 原因 |
| --- | --- |
| `ThirdParty` | 字库和 lwIP，不在阅读路径上 |
| `HAL/Arm64`、`HAL/RiscV` | 另一块板的副本。读 x86 时先跳过 |
| `Services/Desktop` | 23 个文件已经是一个主文件加辅助。再套子目录，开始菜单、托盘、图标要拆开看 |
| `HAL/X64/Drivers/Bus` | 主机控制器已经在一袋。不拆，也不和 xHCI、EHCI 合并 |
| `Drivers/Model` 里的网卡 | Iwl 已经分过 Hw / Mac。核显另见第 8 刀，网卡留下 |
| `Services/Gui`、`Ui` | 窗口系统，不是某一个程序，留在 `Services` 根上 |
| `Drivers/Msc` | U 盘走 USB，不跟 AHCI / ATA / NVMe 放进块设备袋 |
| `Core` | 内存、进程、调度、系统调用规模已经能顺着读 |

## 刀

| 序 | PR | 改哪些 | 验收 |
| --- | --- | --- | --- |
| 1 | **PR-READ-apps** | `FilesUi`、`SettingsUi`、`DevicesUi`、`EditUi`、`TerminalUi` 整目录挪到 `Services/Apps/` 下，目录名不变。`Include/Services/` 里的头不动。`Makefile` 的 `wildcard` 改到新路径。`StoreUi` 留在 `Services/StoreUi/`，下一刀和商店放一起，避免搬两次 | 三架构 `build.sh`。文件、设置、设备、编辑、终端仍能从原来的入口打开 |
| 2 | **PR-READ-store** | `Services/Store`、`Services/StoreUi`、`Services/StoreNet` 收成 `Services/Store/Catalog`、`Services/Store/Ui`、`Services/Store/Net` 三块。主文件名跟着目录。函数名不动。头仍在 `Include/Services/` | `build.sh`。商店列表、安装、联网请求和现在一样 |
| 3 | **PR-READ-shell** | `Services/Console`、`Services/ShellCommands` 收成 `Services/Shell/Console`、`Services/Shell/Commands`。函数名不动 | `build.sh`。内建命令和滚动缓冲仍走原来的入口 |
| 4 | **PR-READ-look** | `Services/Theme`、`Services/Fonts`、`Services/Locale` 收成 `Services/Look/` 下同名三块。`TrueTypeRaster` 的浮点编译规则跟着路径改 | `build.sh`。主题、汉字和语言表仍加载 |
| 5 | **PR-READ-net** | `Services/Tcp`、`Services/LwIp`、`Services/Network` 收成 `Services/Net/` 下三块。教学 TCP 与 lwIP 仍是两套代码 | `build.sh`。两套协议的套接字路径不变 |
| 6 | **PR-READ-input** | `HAL/X64/Drivers/UsbHid`、`Ps2` 收进 `Drivers/Input/`。键鼠报告格式不变 | 三架构 `build.sh`。QEMU 键盘仍能打字 |
| 7 | **PR-READ-block** | `Ahci`、`Ata`、`Nvme` 收进 `Drivers/Block/`。`Msc` 留在 `Drivers/` 根上 | 三架构 `build.sh`。启动盘仍能挂上 |
| 8 | **PR-READ-igpu** | `Drivers/Model/Igpu` 连同 `Igpu.h`、`IgpuAudPin.h` 挪到 `Drivers/Video/Igpu`。`Model` 里只剩网卡 | 三架构 `build.sh`。桌面仍用 GOP，认卡顺序不变 |
| 9 | **PR-READ-driver-headers** | 第 6 到 8 刀还没带走的根上公开头收进各自目录，例如 `Video.h`、`Network.h`、`Hda.h`、`UsbMscHost.h`。包含处改成带目录。`Bus` 内部不动 | 三架构 `build.sh`。显示、网卡、声卡、U 盘的调用点仍编过 |
