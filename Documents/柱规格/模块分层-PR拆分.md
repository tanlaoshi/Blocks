# 模块分层 — PR 拆分

> 状态：10 刀列入 `Documents/路线图新.md`「模块分层」。★ 在第 9 刀。
>
> 依据是 2026-10-19 的源码。三处要收：中断分发点名、设备门面点名、开始菜单把内建程序写死。
>
> 不合并 xHCI 与 EHCI。不合并教学 TCP 与 lwIP。窗口系统各目录在 `Services/Gui/` 下仍分开。`HalIrqRegister` 会换掉 IDT 门，设备中断不能走它，否则会跳过汇编桩。

| 序 | PR | 改哪些 | 验收 |
| --- | --- | --- | --- |
| 1 | **PR-IRQ-table** | `InterruptDispatch`（`HAL/X64/Architecture/Dispatch.c`）对 `VEC_XHCI`、`VEC_E1000`、`VEC_COM1` 查 `HalIrqNotifyRegister` 记下的函数，然后 `LapicEoi` 并返回 0。xHCI 仍先把 `gArchIrqCount` 加一。装 IDT 时登记现在的三个函数：`Irq`、`E1000Irq`、`SerialIrq`。缺页、定时器、系统调用、PIC 向量留在分发函数里。定时器仍可能返回新任务帧。Arm64 / RiscV 的 `HalIrqNotifyRegister` 先空着 | 三架构 `build.sh`。QEMU 里敲一个键能进控制台。没登记的这三个向量只 EOI，不调空函数 |
| 2 | **PR-IRQ-xhci** | xHCI 初始化里调用 `HalIrqNotifyRegister(VEC_XHCI, Irq)`，且在打开这只控制器的中断之前。`ArchIdtLoad` 不再点名 `Irq`。分发文件可以不再包含 `XHCI.h` | `build.sh` + `smoke-boot.sh`。QEMU 键盘仍能打字 |
| 3 | **PR-IRQ-e1000** | E1000 在使能 MSI 之前登记 `E1000Irq`。`ArchIdtLoad` 不再点名 `E1000Irq` | `build.sh`。没有 E1000 的 QEMU 仍能进桌面 |
| 4 | **PR-IRQ-com1** | 串口初始化里登记 `SerialIrq`。`ArchIdtLoad` 不再点名 `SerialIrq`，分发文件不再包含这三只驱动的头 | `build.sh`。串口控制台仍能输出 |
| 5 | **PR-FACE-iwl** | `HalIwlClaim`、`HalIwlReady`、`HalIwlAssociated`、`HalIwlBgBusy` 改调 Iwl 登记的一张表。调用顺序和返回值与现在相同。没登记时与现在「无卡」一样返回 0。`HalDevices.c` 这一段不再包含 `Iwl.h` | `build.sh`。没有 Iwl 的 QEMU 这四个函数仍返回 0 |
| 6 | **PR-FACE-wifi** | `HalWifiClaim`、`HalWifiReady` 改调无线网自己登记的表。认领顺序仍是先 xHCI 再 EHCI。没棒返回 0。`HalDevices.c` 不再包含无线网私有头 | `build.sh`。没有 USB 无线网的 QEMU 仍返回 0 |
| 7 | **PR-FACE-uart** | `HalUsbUartClaim`、`HalUsbUartReady` 只走已经 `Publish` 的 FTDI 与 CDC 操作表。认领顺序不变：先 FTDI（xHCI），已认则跳过；否则在 EHCI 就绪时再认 FTDI；再 CDC。`HalDevices.c` 不再直接调用 `FtdiClaim` | `build.sh`。没有 USB 串口棒时仍返回 0 |
| 8 | **PR-ENUM-virt** | Arm64 的 `HalDeviceEnumerate` 把本机已经 `VirtioBlockRegister` / `VirtioInputRegister` / `VirtioNetRegister` 的设备写成 `DEVICE_NODE`。RiscV 用同一规则，并仍看 `TOY_BOARD_HAS_BLOCK`、`TOY_BOARD_HAS_FRAMEBUFFER`、`TOY_BOARD_HAS_NET`。不扫 PCI。`ToyDriverProbeClass` 的顺序不改 | 三架构 `build.sh`。探测仍只认块设备。没有对应板级开关时不新增节点 |
| 9 | **PR-MENU-rows** | `RebuildStartMenu` 里 Shell、Settings、Files、Store、Devices、Apps、Game、Shutdown、Reboot 这九行改由同一文件里的一张常量表驱动。标签、动作、图标来源、启用与否与现在逐行相同。Apps 二级和 Game 二级仍调用现有的填充函数 | `build.sh`。开始菜单从上到下的项和现在一致 |
| 10 | **PR-MENU-register** | Shell、Settings、Files、Store、Devices 在各自初始化时登记菜单行。`RebuildStartMenu` 按登记顺序铺这五行，顺序必须与第 9 刀的表相同。Apps、Game、Shutdown、Reboot 仍留在桌面这份文件里 | `build.sh`。开始菜单顺序不变。没登记时不凭空多出一行 |
