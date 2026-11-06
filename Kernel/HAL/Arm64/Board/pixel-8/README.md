# Board：`pixel-8`（Google Pixel 8 / shiba · Tensor G3）

> 手机柱日档：[`Archive/路线图1107.md`](../../../../../Archive/路线图1107.md)。  
> **红线**：`fastboot boot` 临时拉起，不 `fastboot flash`；不开 DSI / 触摸 / UFS / USB 主机。

| 项 | 填写 |
|----|------|
| **板名** | `pixel-8`（codename `shiba`） |
| **Arch** | `Arm64` |
| **固件** | Android ABL；`oem uart enable` 后 earlycon |
| **加载** | Linux Image 头 + Android `boot.img`；物理址 `0x10008000` |
| **串口** | Exynos/S3C UART @ `0x10870000`（ttySAC0 / `exynos4210`），115200；主机 USB-Cereal `/dev/ttyUSB0` **1.8V** |
| **能力** | `BRINGUP=1` 串口 `blocks>`；无 FB / 块 / 网 |

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

## 手测（验收 `PR-PIXEL8-1`）

1. CoolTerm / `picocom`：`/dev/ttyUSB0`，115200 8N1（Cereal 电压档认能出字侧 = 1.8V）。
2. 手机 fastboot：`fastboot oem uart enable`（若尚未）。
3. `fastboot boot Build/PIXEL8/HAL/Arm64/boot.img`（**不要** `flash`）。
4. 串口应见早期 `*`/`+`/`Blocks uart`，随后 **`blocks>`**。

---

## 本包文件

| 文件 | 作用 |
|------|------|
| `BoardConfig.h` | UART / 加载址 / Image 头开关 |
| `Board.c` / `Board.h` | 板名；DRAM/FB stub |
| `IdentityMap.c` | BRINGUP 静态恒等页表 |
| `link.ld` | `0x10008000` |
| `scripts/pack-boot-image.sh` | `Kernel.bin` → `boot.img` |
