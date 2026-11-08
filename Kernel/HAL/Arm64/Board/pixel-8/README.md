# Board：`pixel-8`（Google Pixel 8 / shiba · Tensor G3）

> 手机柱活档：[`Archive/路线图1108.md`](../../../../../Archive/路线图1108.md)。  
> **红线**：`fastboot boot` 临时拉起，不 `fastboot flash`；不开 DSI / 触摸 / UFS。

| 项 | 填写 |
|----|------|
| **板名** | `pixel-8`（codename `shiba`） |
| **Arch** | `Arm64` |
| **固件** | Android ABL；`oem uart enable` 后 earlycon |
| **加载** | Linux Image 头 + Android `boot.img`；物理址以 `BoardConfig.h` 为准（`0x90701000`） |
| **串口** | Exynos/S3C UART @ `0x10870000`（ttySAC0），115200；主机 USB-Cereal `/dev/ttyUSB0` **1.8V** |
| **USB** | Tensor G3 `snps,dwc3` @`0x11210000`（`11210000.dwc3`）；★ USB-1 Host 探测 |
| **能力** | `BRINGUP=1` 串口 `blocks>` + `usb: id`/`usb: host`；无 FB / 块 / 网 |

---

## 构建与打包

```bash
cd ~/Blocks/Kernel
./build.sh arm64 BOARD=pixel-8 BRINGUP=1
make ARCH=arm64 BOARD=pixel-8 BRINGUP=1 BUILDDIR=Build/PIXEL8 kernel-bin
./HAL/Arm64/Board/pixel-8/scripts/pack-boot-image.sh
```

产物：`Build/PIXEL8/HAL/Arm64/boot.img`（或脚本打印路径）。

---

## 手测（验收 `PR-PIXEL8-USB-1`）

1. CoolTerm / `picocom`：`/dev/ttyUSB0`，115200 8N1（Cereal **1.8V**；勿用 `ttyUSB1`）。
2. `fastboot oem uart enable`（若尚未）。
3. `fastboot boot Build/PIXEL8/HAL/Arm64/boot.img`（**不要** `flash`）。
4. 串口应见 `blocks>` → `mmu: on` → **`usb: id 0x5533…`** → **`usb: host 0x…`** → 再 `blocks>`。  
   - `usb: no dwc3` / 卡死在 `usb: probe`：报整段；时钟/电源域另刀。  
   - Cereal 须仍能打字（本刀不碰 Type-C mux）。

---

## 本包文件

| 文件 | 作用 |
|------|------|
| `BoardConfig.h` | UART / 加载址 / USB 基址 |
| `Board.c` / `Board.h` | 板名；DRAM/FB stub；USB 门面 |
| `IdentityMap.c` | BRINGUP 静态恒等页表（含 DWC3） |
| `UsbHost/BringUp.c` | DWC3 GSNPSID + PrtCapDir=Host |
| `link.ld` | 链接址 |
| `scripts/pack-boot-image.sh` | `Kernel.bin` → `boot.img` |
