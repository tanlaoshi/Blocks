# Board：`mix-2`（小米 MIX 2 / chiron · MSM8998）

> 手机柱活档：[`Archive/路线图1108.md`](../../../../../Archive/路线图1108.md)。  
> 柱规格：[`Archive/柱规格/手机USB键盘.md`](../../../../../Archive/柱规格/手机USB键盘.md)。  
> **红线**：`fastboot boot` 临时拉起，不 `fastboot flash`；不开 DSI / 触摸 / UFS。

| 项 | 填写 |
|----|------|
| **板名** | `mix-2`（codename `chiron`） |
| **Arch** | `Arm64` |
| **固件** | Qualcomm aboot；控制台 = cont_splash |
| **加载** | gzip 后的 Linux Image + `boot-chiron.img`；phys `0x80008000` |
| **显示** | splash `0x9D600000`；32bpp；stride **4320** |
| **USB** | DWC3 `@0xA800000`（`PR-MIX2-USB-1` 探测） |
| **能力** | `BRINGUP=1` 彩条 + 字；无板上 UART；无 IME（HID Boot 直通） |

---

## 构建与打包

```bash
cd ~/Blocks/Kernel
./build.sh arm64 BOARD=mix-2 BRINGUP=1
make ARCH=arm64 BOARD=mix-2 BRINGUP=1 BUILDDIR=Build/MIX2 kernel-bin
./HAL/Arm64/Board/mix-2/scripts/pack-boot-image.sh
```

产物：`Build/MIX2/HAL/Arm64/boot-chiron.img`。  
Windows 手测拷贝：`\\ASUS\Users\Tank\mix2\boot-chiron.img`。

---

## 手测 · `PR-MIX2-2`（splash ✅）

1. MIX 2 进 fastboot（已解锁）。
2. **`fastboot boot …\boot-chiron.img`**（不要 `flash`）。
3. 顶 **RGBW 彩条** + **`Blocks ready`**。
4. 长按电源回原系统。

**✅ 2026-11-08**：stride=4320；splash=`0x9D600000`。

---

## 手测 · `PR-MIX2-USB-1`（Host 探测 ✅）

`fastboot boot`；电脑占 Type-C；**不要求插坞**。

| 行（小字） | 绿 |
|---|---|
| `id 0x5533….` | Synopsys GSNPSID |
| `host 0x….` | GCTL `PrtCapDir=Host` |

**✅ 2026-11-08**。下一刀 USB-2：boot 后再插坞（柱规格 §7.0）。

---

## 本包文件

| 文件 | 作用 |
|------|------|
| `BoardConfig.h` | 加载址 / splash / bpp / stride / USB 开关 |
| `Board.c` / `Board.h` | 板名；Dram/Fb stub；USB 门面 |
| `IdentityMap.c` | GiB0 DWC3 Device + GiB2 内核/splash |
| `SplashPaint.c` | 彩条 + 点阵；`PaintLine` |
| `UsbHostBringUp.c` | DWC3 GSNPSID / GCTL Host |
| `link.ld` | `0x80008000` |
| `scripts/pack-boot-image.*` | `Kernel.bin` → `boot-chiron.img` |
