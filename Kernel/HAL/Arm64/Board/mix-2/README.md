# Board：`mix-2`（小米 MIX 2 / chiron · MSM8998）

> 手机柱日档：[`Archive/路线图1107.md`](../../../../../Archive/路线图1107.md)。  
> **红线**：`fastboot boot` 临时拉起，不 `fastboot flash`；不开 DSI / 触摸 / UFS / USB 主机。

| 项 | 填写 |
|----|------|
| **板名** | `mix-2`（codename `chiron`） |
| **Arch** | `Arm64` |
| **固件** | Qualcomm aboot；控制台 = cont_splash |
| **加载** | gzip 后的 Linux Image + `boot-chiron.img`；phys `0x80008000`（对表线刷 `kernel_addr=0x8000`） |
| **显示** | splash `0x9D400000` / `0x2400000`；32bpp；stride 4352 |
| **能力** | `BRINGUP=1` 顶彩条 + `Blocks ready`；无 UART |

---

## 构建与打包

```bash
cd ~/Blocks/Kernel
./build.sh arm64 BOARD=mix-2 BRINGUP=1
make ARCH=arm64 BOARD=mix-2 BRINGUP=1 BUILDDIR=Build/MIX2 kernel-bin
./HAL/Arm64/Board/mix-2/scripts/pack-boot-image.sh
```

产物：`Build/MIX2/HAL/Arm64/boot-chiron.img`。

---

## 手测（验收 `PR-MIX2-2`）

1. MIX 2 进 fastboot（已解锁）。
2. **`fastboot boot Build/MIX2/HAL/Arm64/boot-chiron.img`**（不要 `flash`）。
3. 屏上开机 logo / splash 顶部应见 **RGBW 彩条**，其下 **`Blocks ready`**。
4. 长按电源回原系统（未写 boot 分区）。

**PR-MIX2-2 ✅（2026-11-08）**：真机见彩条 + `Blocks ready`；stride=4320；splash=`0x9D600000`。

---

## 本包文件

| 文件 | 作用 |
|------|------|
| `BoardConfig.h` | 加载址 / splash / bpp / stride |
| `Board.c` / `Board.h` | 板名；Dram/Fb stub |
| `IdentityMap.c` | 内核窗 + splash 恒等页表 |
| `SplashPaint.c` | 彩条 + 8×8 点阵字 |
| `link.ld` | `0x80008000` |
| `scripts/pack-boot-image.*` | `Kernel.bin` → `boot-chiron.img` |
