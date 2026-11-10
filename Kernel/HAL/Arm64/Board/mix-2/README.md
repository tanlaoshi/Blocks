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
| **显示** | splash `0x9D600000`；1080×2160；stride **4320**；相位 X **+592** ✅ / Y **0**（勿用 4352） |
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

**✅ 2026-11-08**。

---

## 手测 · `PR-MIX2-USB-2`（hub 热插 ✅）

1. `fastboot boot` → **`wait dock`**  
2. 拔电脑 → 坞 + **墙充**  
3. 绿：`e a1`

---

## 手测 · `PR-MIX2-USB-3`（HID 键 ✅）

1. 同上换线；USB-A 插键盘。  
2. 绿：`e a1` + **`k a2`**；敲键 splash **`KEY>`** 出字。  
3. **热插**：拔坞 → `wait dock`；插回 → 再 `e a1`/`k a2`（须 DisableSlot 拆树）。  
4. 短按电源 → fastboot；再刷机须 **电脑线直接插手机**（单口物理；坞口接不了 PC）。

---

## 手测 · `PR-MIX2-USB-3c`（Caps/Num/Scroll 锁灯 · 本刀）

> **禁**：Configure / 首中断发 SET_REPORT（Pixel → `2711`）。只锁键翻转。

1. USB-3 绿后按 **Caps / Num / Scroll**。  
2. 绿：灯跟 + sticky **`4Dxx`**（xx=LED 字节）。  
3. 红：`4Ecc`＝SET_REPORT 败（把码发回）；`2711`＝又踩早发。

---

## 手测 · `PR-PHONE-PWR-0`（短按 → fastboot ✅）

电脑可仍占 Type-C（不要求插坞）：

1. `fastboot boot …\boot-chiron.img` → 彩条 + USB 字；须见 **`usb hub`** + 状态行含 **`pon…`**。  
2. 空闲看 `pon`：正常小值（常 `0`）；若像 `80000000`＝PON SPMI 读失败。  
3. **按住**电源看 `pon` bit0 是否变 1；松手回 0。变了再短按应出 **`pwr fastboot`** → `fastboot devices`。  
4. **长按**仍硬关。  
5. hub：拔电脑线→插坞+墙充；绿仍 **`e a1`**（与 USB-2 同）。

---

## 本包文件

| 文件 | 作用 |
|------|------|
| `BoardConfig.h` | 加载址 / splash / bpp / stride / USB 开关 |
| `Board.c` / `Board.h` | 板名；Dram/Fb stub；USB 门面 |
| `IdentityMap.c` | GiB0 DWC3/SPMI/PS_HOLD Device + GiB2 内核/splash |
| `SplashPaint.c` | 彩条 + 点阵；`PaintLine` |
| `UsbHost/BringUp.c` | DWC3 GSNPSID / GCTL Host |
| `UsbHost/PonPowerButton.c` | PM8998 PON：短按 → reboot cookie + PS_HOLD |
| `UsbHost/QusbPhy.c` | QUSB2 DT init-seq + PLL ready |
| `UsbHost/Qscratch.c` | QSCRATCH UTMI-as-PIPE + Host VBUS override |
| `UsbHost/SpmiPmicArbiter.c` | SPMI PMIC arbiter v2 读写 |
| `UsbHost/TypeCForceHost.c` | PMI8998 Type-C DFP（`0x1368`） |
| `UsbHost/Private.h` · `Utility.c` | USB 共用 |
| `UsbHost/XhciStart.c` · `XhciTransfer.c` · `XhciControl.c` | xHCI 起环 / 命令 / EP0 |
| `UsbHost/HubWatch.c` | 根口热插 + hub 编址 |
| `link.ld` | `0x80008000` |
| `scripts/pack-boot-image.*` | `Kernel.bin` → `boot-chiron.img` |
