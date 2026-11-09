# Board：`pixel-8`（Google Pixel 8 / shiba · Tensor G3）

> 手机柱活档：[`Archive/路线图1108.md`](../../../../../Archive/路线图1108.md)。  
> **红线**：`fastboot boot` 临时拉起，不 `fastboot flash`；不 init DSI/触摸/UFS；**可写 ABL splash**。

| 项 | 填写 |
|----|------|
| **板名** | `pixel-8`（codename `shiba`） |
| **Arch** | `Arm64` |
| **固件** | Android ABL |
| **加载** | Linux Image 头 + Android `boot.img`；phys `0x90701000` |
| **显示** | ABL splash **`0xFAC00000`**；1080×2400；stride **4320**；PHASE_X **0** ✅；DECON TRIG **`0x19470030=0x3061`** |
| **串口** | 备份：Exynos UART @ `0x10870000`；Cereal 1.8V |
| **USB** | USB-1 ✅；★ USB-2 hub（屏反馈 + 坞） |
| **能力** | `BRINGUP=1`：MMU + DECON TRIG + splash |

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

## 手测 · `PR-PIXEL8-2`（splash ✅）

1. `fastboot boot …/boot.img`（可不插 Cereal）。  
2. ✅ 2026-11-09：左上起 **RGBW 彩条** + **`Blocks ready`**（须 DECON TRIG）。  

## 手测 · `PR-PIXEL8-USB-2`（hub ★）

1. `fastboot boot` → 彩条齐。  
2. **拔电脑线** → 插 Type-C 坞（外供电）。  
3. 绿：屏状态行见 CONNECT / **`e…a1`**。

---

## 本包文件

| 文件 | 作用 |
|------|------|
| `BoardConfig.h` | UART / 加载址 / splash / USB 开关 |
| `Board.c` / `Board.h` | 板名；Dram/Fb stub；splash/USB 门面 |
| `IdentityMap.c` | BRINGUP 恒等页表（UART + splash GiB3） |
| `SplashPaint.c` | 彩条 + 点阵 |
| `UsbHost/*` | Host/hub（本刀 `BOARD_PIXEL8_USB_HOST_BRINGUP=0`） |
| `scripts/pack-boot-image.sh` | `Kernel.bin` → `boot.img` |
