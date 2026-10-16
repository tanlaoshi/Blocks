# Iwl

Intel 8265/8275 无线驱动（PCIe `8086:24FD`）。灌 `FW/IWL8265.UCODE`，读 `FW/WIFI.CFG`，关联后由 `NetIwl` 上网。与 USB `Wifi/`（8188EU）无关。

目录按 Linux 的分法：`Hw/` 对芯片侧 `iwlwifi`/`iwlmvm`，`Mac/` 对 `mac80211` 里扫、关联、四次握手那一层。Blocks 只分目录，不另做全核无线栈。

| 目录 | 看什么 |
| --- | --- |
| `Hw/` | 认卡、MMIO、灌固件、TX/RX 队列、Alive、Paging。`Hw/IwlPhy` 收校准，`Hw/IwlMvm` 把已决定的事写成固件命令 |
| `Mac/` | 扫 AP、关联、EAPOL。**四次握手在 `Mac/IwlEapol/`** |
| `Crypto/` | AES/SHA/TKIP/CCMP/PBKDF，仍姓 Iwl，只给 Mac 用 |

根上留下对外头 `Iwl.h`、网卡胶水 `NetIwl.c`、共享状态 `IwlState.h`。`IwlPrivate.h` 只转发 Hw 与 Mac 两份私头。`IwlMac.c` 是读芯片 MAC 地址，不是 `Mac/` 里的协议。

开机 Claim 在 Worker：桌面起来之后才 `IwlSetup`，固件和关联继续在 Worker 里泵。文件系统和网络的早 Probe 不进这条路径。无卡软失败，不挡启动。

规格：[Iwl目录Hw-Mac-PR拆分.md](../../../../../Documents/柱规格/Iwl目录Hw-Mac-PR拆分.md)
