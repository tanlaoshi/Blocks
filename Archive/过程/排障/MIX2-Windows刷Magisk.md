# MIX 2 · Windows 刷 Magisk（临时手测）

> 背景：本机 Linux 上 `fastboot flash boot` 传 ~19MB 会卡死；改到 Windows 刷。  
> 机型：`chiron`（小米 MIX 2）· 序列号 `d9c6f99c` · 已解锁。  
> 补丁镜像已打好，**不必**再在手机里点 Magisk「修补」。

---

## 0. 重启 Windows 之前（Linux 已做好）

补丁文件已放在 Windows 能直接看到的盘：

| 位置 | 路径 |
|---|---|
| **DATA 盘（推荐）** | `D:\mix2-magisk\magisk_patched-31100_mXWHj.img`（Linux 挂载为 `/media/tank/DATA/mix2-magisk/`；盘符以资源管理器为准，可能是 `D:` / `E:`） |
| 手机里也有一份 | 系统里 `Download/magisk_patched-31100_mXWHj.img`（可作备份） |

确认 DATA 里有该文件后再重启进 Windows。

---

## 1. Windows 装工具（从零）

### 1.1 platform-tools（adb / fastboot）

1. 浏览器打开：  
   https://developer.android.com/tools/releases/platform-tools  
2. 下载 **SDK Platform-Tools for Windows**（zip）。  
3. 解压到例如：`C:\platform-tools\`  
   内应有 `adb.exe`、`fastboot.exe`。  
4. 打开 **命令提示符** 或 **PowerShell**：

```bat
cd C:\platform-tools
.\fastboot.exe --version
```

能输出版本号即可（不必装 Android Studio）。

### 1.2 驱动（任选其一，能认出 fastboot 即可）

小米 fastboot 常用 Google 通用驱动：

1. 打开：https://developer.android.com/studio/run/win-usb  
2. 下载 **Google USB Driver**，或装 [universal adb driver](https://adb.clockworkmod.com/) / 「小米万能驱动」任选。  
3. 手机进 fastboot 后，设备管理器里应看到 **Android Bootloader Interface** / **Android Device**，无黄色感叹号。

若只有「未知设备」：右键 → 更新驱动 → 浏览到 Google USB Driver 目录。

---

## 2. 刷入（核心三步）

1. 手机开机到系统，用 **USB2 线** 接电脑（就是 Linux 上测过 40MB/s 那根）。  
2. 开启「USB 调试」（若 Windows 弹 RSA 指纹就允许）。  
3. 在 `C:\platform-tools` 里执行：

```bat
.\adb.exe devices
.\adb.exe reboot bootloader
```

4. 手机应显示 Fastboot。然后：

```bat
.\fastboot.exe devices
.\fastboot.exe flash boot D:\mix2-magisk\magisk_patched-31100_mXWHj.img
.\fastboot.exe reboot
```

把 `D:` 换成资源管理器里 DATA 盘的实际盘符。

**正常现象**：几秒内出现 `Sending` → `Writing` → `OKAY`。  
若又长时间无进度：换 USB 口（尽量主板后置 USB2）、换线，或把 img 拷到 `C:\platform-tools\` 再：

```bat
.\fastboot.exe flash boot magisk_patched-31100_mXWHj.img
.\fastboot.exe reboot
```

---

## 3. 验收

进系统后：

1. 打开 **Magisk** App → 顶部 Magisk 一行应显示**版本号**（不再是「未安装」）。  
2. 若提示需要「额外设置 / 重启」，按提示做一次。  
3. 再装回 Linux（或仍在 Windows）用 adb：

```bat
.\adb.exe shell su -c id
```

应看到 `uid=0(root)`。

通过后回到 Linux 对话说一声，继续 `PR-MIX2-1`（读 `fb0` bpp / stride / splash `reg` → `BoardConfig.h`）。

---

## 4. 失败时

| 现象 | 处理 |
|---|---|
| `fastboot devices` 空 | 驱动；换口/线；设备管理器更新 Google USB Driver |
| `flash` 又卡住 | 换口；img 放到 `C:\platform-tools\` 再刷；或另找一台 Windows 笔记本 |
| 开机卡 Logo / 循环 | 用官方线刷包 `V12.0.1.0.PDECNXM` 的 `boot.img` 刷回：`fastboot flash boot boot.img`，再重试 |
| Magisk 仍「未安装」 | 确认刷的是 `magisk_patched-*.img` 不是原版 `boot.img` |

**不要**对 MIX 2 刷 Pixel 的包；**不要**用 `ck-pi-sd` 一类脚本。  
Blocks 内核仍只允许以后 `fastboot boot`，本次只写 Magisk 的 stock boot。

---

## 5. 文件清单（本机）

| 文件 | 用途 |
|---|---|
| `DATA\mix2-magisk\magisk_patched-31100_mXWHj.img` | 要刷的 Magisk boot |
| 手机 `Download\magisk_patched-31100_mXWHj.img` | 同内容备份 |
| 官方 `boot.img`（线刷包内） | 救砖 / 回退 |
