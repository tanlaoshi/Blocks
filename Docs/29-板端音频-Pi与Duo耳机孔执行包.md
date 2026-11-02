# 29 · 板端音频（Pi + Duo 耳机孔）— DeepSeek 执行包

> **给执行者（DeepSeek）**：按 ★ **一刀**做；一次一变量；未过判据 **不 TG / 不 push**（用户已授权刀绿后 Cursor 可自动 TG+TS，DS 仍须等评审 ✅）。  
> **给评审（Cursor）**：对表 §8 红线；填本刀表末 **评审**（与 Docs/24/25 同形）。  
> **目标**：三架构同形 `HalAudio*`；**Pi / Duo 3.5 mm 耳机孔能听见**蜂鸣 / `play`。  
> **标杆**：X64 Intel HDA 柱已收官（[`声卡驱动-HDA`](../Archive/过程/柱归档/声卡驱动-HDA.md)；`play`/`beep`/`BEEP.WAV`）。  
> **活 ★**：[`路线图1102`](../Archive/路线图1102.md) = **`audio-eyes-0`**。信箱：[`TO-DS.md`](../Archive/协作信箱/TO-DS.md)。  
> **回归总表**：[`Docs/28`](28-PI-DUO-WiFi-BT-GPIO回归计划.md)（本柱绿后补「声音」节）。

### 协作闭环

| 谁 | 动作 |
|---|---|
| **DeepSeek** | 填「进度」→ 等评审 **`✅ 全绿`** → TG+TS → 等下一刀 JX；盯 TO-DS |
| **Cursor** | 进度更新 → 填「评审」+「给 DS」；切刀改 TO-DS / 1102 ★ |
| **开刀条件** | TO-DS / 1102 ★ = 本文当前刀名 |
| **第一刀** | `audio-eyes-0`（眼睛 → 交付 Docs/30 Pi + Docs/31 Duo，或本文 §6 填满） |

---

## 0 · 目标与非目标

| 要 | 不要 |
|---|---|
| `HalAudioProbe` / `PlayPcm` / `Stop` / `Beep` 在 Pi、Duo **真播**（耳机可闻） | 空实现 `PlayPcm` return 非 0/恒「成功」却无声 |
| 壳 `beep` / `play`（无参=蜂鸣；WAV 48k/16/1–2ch）在 **ConsoleOnly** 也可用 | 只改 X64；VirtMin 不挂命令却声称 Duo 可测 |
| 对表 **厂商 dts + Linux 驱动** / 本仓 X64 `HalAudio*` 语义 | 猜基址、扫寄存器、狂改时钟「碰运气」 |
| 命名写全、单文件 ≤300；板差只在 Board/Audio | `Cfg`/`Init`/`Reg`/`Buf`/`Dma` 等禁缩写顶全称 |
| 失败软退、不卡死壳、不挡 `Blocks>` | 为出声改 WiFi / BT / 串口脚 |

**本包最小闭环**

1. `HalAudioProbe() != 0`（有通路）  
2. `beep`（或 `play` 无参）→ **耳机孔有声**（人耳判据；串口可有 `aud:`/`play: beep`）  
3. 可选：`play Assets/…/BEEP.WAV`（或板载拷贝）→ `play: ok` 且有声  
4. 冷启可重复；失败打印 `play: no audio` / 诊断串，**不**假绿  

**本包明确不做**

- 录音 / 混音 / 多卡  
- Pi **HDMI** 音频（另柱）  
- 蓝牙 A2DP / SCO  
- 完整 ALSA；用户态音量 UI  
- Duo 接屏相关（已决策 console-only）

---

## 1 · 为何现在无声（复现）

```text
# Duo / Pi ConsoleOnly 现状
Blocks> play
unknown: play          # VirtMin 未挂 Audio（Duo 必现；Pi 若 ConsoleOnly 同）
# 或（若强行链了命令但 Hal 空桩）：
Blocks> play
play: no audio         # HalAudioProbe()==0
```

| 架构 | 文件 | 现状 |
|---|---|---|
| X64 | `HAL/X64/Drivers/Hda/*` + `HalDevices.c` | ✅ Probe/PlayPcm/Beep |
| Arm64 (Pi) | `HAL/Arm64/Hal/HalDevices.c` | `HalAudio*` **空**：Probe=0；PlayPcm 空 return 0 |
| RiscV (Duo) | `HAL/RiscV/Hal/HalDevices.c` | 同上空桩 |
| 壳 | `Services/Shell/Commands/Audio.c` | ✅ `play`/`beep`；仅 **`ShellCommandsRegister`（桌面表）** 注册 |
| ConsoleOnly | `Commands.c` → `ShellCommandsRegisterVirtMin` | ❌ **未** `ShellCommandsAudioRegister` |

门面权威：`Include/Hal/HalDevices.h`（约 122–126 行）。

---

## 2 · 硬件边界（动手前必读 · 眼睛刀须钉死）

### 2.1 Pi 3B · AV 孔 = PWM 模拟

| 项 | 钉死口径（引用，禁改口胡猜） |
|---|---|
| 物理 | 板载 **3.5 mm AV**（视频+音频）；音频来自 **PWM0/PWM1** |
| 控制器 | `pwm@7e20c000`，`compatible = "brcm,bcm2835-pwm"`（`bcm283x.dtsi`） |
| Pi 3B 总线窗 | SoC `0x7E20C000` → CPU **`0x3F20C000`**（与其它 BCM 外设同映射） |
| 引脚 | `bcm2837-rpi-3-b.dts`：`&pwm { pinctrl-0 = <&pwm0_gpio40 &pwm1_gpio41>; }` → **GPIO40 / GPIO41** |
| 时钟 | dts：`BCM2835_CLOCK_PWM`，`assigned-clock-rates = <10000000>` |
| Linux 对照 | `pwm-bcm2835` + 用户态/ALSA `snd_bcm2835`（经典「模拟耳机」路径）；**本仓实现优先最小：PWM 方波/PCM 调制蜂鸣 → 再考虑 DMA** |
| 勿抢 | **GPIO14/15**（壳 mini-UART）；**GPIO32/33**（BT PL011）；WiFi SDIO 脚 |
| 部署 | `ksend --path BLOCKS:Kernel.elf` 或 `ck-pi-sd`；TTL 接壳 |

**audio-eyes-0 · Pi 列必须填的表**（写入 Docs/30 或本文 §6）：

| # | 问题 | 必须有答案 |
|---|---|---|
| 1 | PWM 基址（CPU 窗）？时钟谁开？ | 对表 dts + 本仓若已有 clock/mbox 助手 |
| 2 | GPIO40/41 复用寄存器怎么切到 PWM？ | 对表 GPFSEL / 现网 `HalGpio` 是否可复用（音频脚**勿**进 GPIO 白名单乱拧） |
| 3 | 最小可闻路径：固定占空比方波 **还是** 必须上 DMA PCM？ | 写死；蜂鸣允许方波 |
| 4 | `HalAudioPlayPcm` 格式？ | 对齐壳：优先 **48 kHz / 16-bit / 1–2ch**（与 X64 `play` 同） |
| 5 | 与 HDMI 声关系？ | 本柱 **只做 AV 孔**；HDMI 另柱 |

### 2.2 Duo S · 3.5 mm = 片内 DAC + I2S3

| 项 | 钉死口径 |
|---|---|
| 物理 | **本板无 3.5 mm**。孔在 **PoE HAT** 上：SoC DAC → 排针 `AUDIO_OUT_L` / `AUDIO_OUT_R` → HAT 座。手测须 **插着 PoE HAT**；只插主板 = 无孔可听 |
| DAC | `dac@0300A000`，`compatible = "cvitek,cv182xadac"`，`reg = <0x0 0x0300A000 0x0 0x100>`（`cv181x_base.dtsi`） |
| ADC（本柱不做录音） | `adc@0300A100`；排针另有 `AUDIO_IN_*`，**不**进 HAT 耳机座（座非麦聚合） |
| I2S TX→DAC | **`i2s3@04130000`**：`capability = "tx"`；注释 **「I2S3 connect to internal DAC as TX」**；`dmas` 仅 write；`mclk_out = "true"` |
| I2S0 | 注释为 internal ADC RX — **播放勿走错** |
| 时钟 | `audio_clock` 默认 **24576000**；`i2s_mclk` / `i2s_subsys` |
| Linux 对照 | `sound/soc/cvitek/cv181xdac.c`、`cv181x_cv181xdac.c`、`cv1835_i2s.c`；寄存器头 `sound/soc/codecs/cv181xadac.h`（`AUDIO_PHY_TXDAC_*`） |
| mute-gpio | **Duo S 板 dts 删除** `mute-gpio-l/r`（`sg2000_milkv_duos_*_sd.dts` `&dac { /delete-property/ …}`）→ 眼睛刀须确认：**无外置 mute 脚**还是另有电源；**禁止**为 unmute 去拉 WiFi 的 GPIOA15 |
| 声卡节点 | `sound_dac` = `cvitek,cv182xa-dac`；`cviaudio_core` |
| 勿碰 | UART0 壳；WiFi SDIO/`Wifi*`；BT UART4；GPIOA15 上电 |
| 部署 | ksend **`BOOT:boot.sd`**（FIT）；禁 `ck-pi-sd` |

**audio-eyes-0 · Duo 列必须填的表**（Docs/31 或本文 §6）：

| # | 问题 | 必须有答案 |
|---|---|---|
| 1 | 播放数据路径：I2S3 TX → DAC@`0x0300A000`？ | 对表 dts 注释 + 驱动 |
| 2 | 时钟门控 / reset 谁开？基址？ | 对表 clk / soft_rst；引用文件行 |
| 3 | 最小路径：CPU 轮询写 I2S FIFO **还是** 必须 SDMA？ | 写死；蜂鸣可先 CPU |
| 4 | DAC 上电/模拟偏置寄存器最小集？ | 对表 `cv181xdac` 初始化，**抄厂商序** |
| 5 | mute：板级无 mute-gpio 时孔是否常通？ | 否证或实测 |
| 6 | `HalAudio*` 放哪？ | `HAL/RiscV/Board/milk-v-duo-s/Audio/*.c`（建议） |

### 2.3 X64 对表（只抄契约）

| 抄 | 不抄 |
|---|---|
| `HalAudioProbe/PlayPcm/Stop/Beep` 语义；`Samples==NULL`→内置蜂鸣 | PCI / CORB / BDL / HDMI ELD |
| 壳 `play` WAV 约束（48k/16） | HDA 厂商 quirk 表整份搬 |
| 失败不挡 ready | 桌面 Ready 自动蜂鸣策略（ConsoleOnly 无桌面） |

---

## 3 · 架构落点（拟改 / 新建）

### 3.1 推荐目录

| 路径 | 职责 |
|---|---|
| `HAL/Arm64/Board/rpi-3b/Audio/` | PWM 时钟、pinmux、播蜂鸣/PCM（多文件 ≤300） |
| `HAL/RiscV/Board/milk-v-duo-s/Audio/` | I2S3 + DAC 初始化与送数 |
| `HAL/Arm64/Hal/HalDevices.c` | `HalAudio*` → 调 Board |
| `HAL/RiscV/Hal/HalDevices.c` | 同上 |
| `Services/Shell/Commands/Commands.c` | **`ShellCommandsRegisterVirtMin` 增加 `ShellCommandsAudioRegister`**（audio-shell-1） |
| `Services/Shell/Commands/Audio.c` | 尽量不动；格式已钉 |

### 3.2 建议文件名（全称、≤300）

**Pi**：`PwmTransport.c` / `PwmClock.c` / `WavePlayback.c` / `BringUp.c`（可按眼睛结论改名，禁 `Aud.c`/`Pwm.c` 砍字）。  
**Duo**：`I2sTransmit.c` / `DigitalToAnalog.c` / `WavePlayback.c` / `BringUp.c`。

### 3.3 返回值约定（与壳对齐）

| API | 成功 | 失败 |
|---|---|---|
| `HalAudioProbe` | **非 0** | **0**（壳：`play: no audio`） |
| `HalAudioPlayPcm` | **非 0** | **0** |
| `HalAudioBeep` | void；内部调 `PlayPcm(NULL,…)` | — |

⚠ 现 Arm/RiscV 空桩：`PlayPcm` **return 0** 与「失败」同形，但 Probe 也是 0 → 壳进不了播。接板后 Probe 必须真反映硬件。

---

## 4 · 刀序（★ 从上往下）

### audio-eyes-0 · 通路眼睛（当前 ★）

| 项 | 内容 |
|---|---|
| **做** | 填 §6 对表表（Pi 列 + Duo 列）；产出 Docs/30、Docs/31 **或** 本节填满不另开文件；串口可打 `Audio: eyes ok`（可选） |
| **判据** | 表无空结论；评审能点开 dts/驱动行号；**无行为代码**（或仅一眼日志） |
| **禁止** | 扫 PWM/DAC；改 Wifi/BT；假 Probe=1 |
| **手测** | 不要求有声；文档抽查即可 |
| **进度** | ✅ **收官（2026-11-02 08:20 DS → 08:35 Cursor 纠时钟）**：§6.1/§6.2 填满；无行为代码。评审纠回 Pi `assigned-clock-rates`=**10 MHz**（非 50）。 |
| **评审** | ✅ **全绿**（2026-11-02 08:35 Cursor）。抽查：Duo I2S3/DAC/mute/audio_clock 行号齐；Pi 基址/ALT0/CM_PWMCTL 齐。**红点已纠**：`bcm283x.dtsi:418` 实为 `<10000000>`（**10 MHz**），DS「改成 50 MHz」是反更正——§6.1#3 与结语已改回。`bcm2837-rpi-3-b.dts` `&pwm` 实为 **:121-125**（非 115-118）。人耳/VirtMin/Probe 本刀不适用。**给 DS**：可 **TG+TS**（仅 Docs/29 + 信箱/1102 ★）；再 **`JX audio-pi-1`**，四文件按 §3.2（`PwmClock`/`PwmTransport`/`WavePlayback`/`BringUp`）；时钟按 **10 MHz** 编程。 |

### audio-pi-1 · PWM → 耳机可闻

| 项 | 内容 |
|---|---|
| **做** | Board Audio + `HalAudio*`；`beep` 孔响；可选短 PCM |
| **判据** | 真机：`play`/`beep` → **人耳有声**；串口 `play: beep` 或 `aud: … ok`；壳 UART 不断 |
| **禁止** | 抢 GPIO14/15；动 BT UART；空桩假绿 |
| **手测** | Pi + 耳机；ksend 后冷启 |
| **进度** | ✅ **否证收官（2026-11-02 09:40）**：用户澄清**从未有任何声/咔哒**；扫频/占空/on·off/三极四极耳机全无声；SoC 寄存器齐。 |
| **评审** | ✅ **否证全绿（收口）**（2026-11-02 09:42 Cursor）。认账：咔哒线索作废；A/B/D 排除；人耳判据不成立 → **不假绿**。选 **方案 1**：本刀否证收口；`audio-pi-1b`（VCHIQ）/换板 **点名再开**，不自动 ★。代码探针可留仓；`CMD_MAX=72` 真修可同捆 TG。⚠ `PwmTransport` 注释仍把 MSEN 写成「从 FIFO 取数」——**错**（MSEN=Mark-Space；FIFO 要 USEF），否证不挡，duo/pi-1b 前改掉。**给 DS**：可 **TG+TS**（Audio 探针+CMD_MAX+Docs/29）；★ → **`audio-duo-1`**；勿开 VCHIQ 除非用户点名。 |
| **进度（DS 实时）** | ❌ **否证（2026-11-02 09:40）**：代码与寄存器级证据齐全，但**人耳判据不成立**。<br>**证据**：`cm_ctl=0x91`（ENAB+BUSY）、`cm_div=0x1eb8`（10.000 MHz）、`rng1=10000`、`gpfsel4=0x24200924`（pin40/41 ALT0）⇒ SoC 配置全对；**频率 1/100/200/300/500/1k/2k/3k/5k Hz、占空比 50/90/100%、MS 与 MSEN 两种模式、三极与四极耳机各一副** —— 全部**无声**（同一副耳机在别的电脑可用）。<br>**已排除**：A（`audio_pwm_mode=1` + `dtparam=audio=on/off` 都试过）、B（波形/占空比/耳机类型）、D（耳机/插头）。<br>**剩 C**：固件音频通路未上电/被 mute（VCHIQ `snd_bcm2835`），或该台 Pi 3.5 mm 座硬件问题（无对照机）。<br>**按评审 08:55 规矩：不写绿、不 TG**；下一步三选一（收口否证 / 换台 Pi 交叉验证 / 转 audio-pi-1b 做最小 VCHIQ）见 FROM-DS 09:40。<br>**附带真 bug**：`CMD_MAX=48` 满表导致后注册命令静默丢弃（`audioreg` 曾 unknown），已抬到 72。 |

### audio-duo-1 · I2S3+DAC → 耳机可闻

| 项 | 内容 |
|---|---|
| **做** | Board Audio + `HalAudio*`；对表 cvitek 初始化序 |
| **判据** | 同 pi-1；**不**改 `Sdhci/Wifi*` / BT |
| **禁止** | 拉 GPIOA15「当 mute」；走错 I2S0 |
| **手测** | Duo + **PoE HAT** + 耳机（插 HAT 孔）；**boot.sd** 部署 |
| **进度** | ✅ **真机出声（2026-11-02 10:15，人耳判据成立）**<br>**实现**：`RiscV/Board/milk-v-duo-s/Audio/{AudioPrivate.h,I2sTransmit.c,DigitalToAnalog.c,WavePlayback.c,BringUp.c,AudioDiagnostics.c}`；`HalAudio*` 已接；`BOARD_HAS_AUDIO=1`。I2S3 主机 TX（48 kHz/16-bit/2ch）→ 片内 DAC（`CTRL0 |= TXDAC_EN|I2S_RX_EN`），CPU 写 TX FIFO 送方波。<br>**判据串口原文**（build `Nov 2 2026 10:12:45`）：`duoaudio regs` → `en1=ffffffff blk=00000003 frame=000f000f slot1=000f0f00 clk1=00100001 enable=00000001 dac_ctrl0=00000003`；`duoaudio fifo` → `before=00000000 after=00000000`；`play` → **`play: beep`**；**人耳：听到"滴滴"两声（= DUO_AUDIO_BEEPS=2）**。<br>**两个真 bug（本刀修）**：① `TX_STATUS(0x48)` 低 5 位**不是**"FIFO 剩余空位"（该寄存器只有 bit23 有文档）⇒ 原先写入循环条件恒不成立，**一帧都没写进去**；改成"每毫秒批量写 32 帧"（FIFO 深 32，1 ms 取空）。② `play` 只在桌面表注册 ⇒ Duo（VirtMin）上 `unknown`；补 `ShellCommandsAudioRegister()` 与 `ShellCommandsDuoAudioRegister()` 到 VirtMin。<br>**已知偏差**：批量 32 帧/ms 使有效采样率 32 kHz（48k 的 2/3），1 kHz 实际约 667 Hz；先求"能响"，音准留 duo-1b（DMA/正确节流）。 |
| **评审** | ✅ **全绿**（2026-11-02 10:16 Cursor）。人耳「滴滴」+ `play: beep` + regs/FIFO 原文齐；I2S3→DAC 路径对；≤300；`ElapsedMilliseconds` 已改（禁 `Ms`）。音准 32 kHz 有效采样**明示**留 **`audio-duo-1b`**（本刀只判能响，不假 48 k）。VirtMin 挂 `play`/`duoaudio` = shell-1 最小实现，并入本刀。**给 DS**：可 **TG+TS**；★ 清空（`duo-1b` 点名再开）。 |

### audio-shell-1 · VirtMin 挂 play/beep

| 项 | 内容 |
|---|---|
| **做** | `ShellCommandsRegisterVirtMin` 调 `ShellCommandsAudioRegister`；确认 alias `beep` |
| **判据** | Duo/Pi ConsoleOnly：`help` 见 `play`；`beep` 不 `unknown`；有声（依赖 pi-1/duo-1） |
| **禁止** | 改命令语义偏离 X64 |
| **进度** | ✅ **并入 audio-duo-1**（2026-11-02）：VirtMin 已调 `ShellCommandsAudioRegister` + `ShellCommandsDuoAudioRegister`；真机 `play: beep` |
| **评审** | ✅ **全绿**（随 duo-1；不单开） |

> **刀序说明**：eyes → **建议 Pi 先于 Duo**（PWM 路径更短）；shell-1 可与 duo-1 前后，但 **无 Hal 通路时挂命令只能测到 `play: no audio`**，宜在至少一板 Probe 绿后做。允许 shell-1 与 pi-1 同会话末尾小改 Commands.c（仍一次一变量优先）。

---

## 5 · 手测脚本（收官用）

### 5.1 Pi（`@ttyUSB1`）

```text
help                    # 须有 play（shell-1 后）
play                    # 或 beep：耳机有声；串口 play: beep
play /Assets/Sounds/BEEP.WAV   # 若文件在卡上；play: ok + 有声
# 回归：壳仍可输入；bt / wifi 抽测不崩
```

### 5.2 Duo（`@ttyUSB0`）

```text
help
play
# 期望：有声；非 unknown；非长期卡死
# 回归：gpio selftest 18 PASS；wifiprobe 或 bt 不要求本柱重跑全套
```

### 5.3 收口模板

```text
audio-*: 板=Pi|Duo 口=ttyUSB* 
原文：…
人耳：有声 / 无声
Probe= … Play= …
交叉：壳 UART=OK / 否
```

---

## 6 · 对表表（audio-eyes-0 交付）

### 6.1 Pi 列（audio-eyes-0 已填）

| # | 问题 | 结论 | 证据（文件:行） |
|---|---|---|---|
| 1 | PWM CPU 基址？ | **`0x3F20C000`**（寄存器窗 0x28 B：CTL/STA/DMAC/RNG1/DAT1/FIF1/RNG2/DAT2） | 上游 `bcm283x.dtsi:413-415`：`pwm: pwm@7e20c000 { compatible="brcm,bcm2835-pwm"; reg=<0x7e20c000 0x28>; }`；CPU 窗换算与本仓一致（GPIO `0x7E200000→0x3F200000`，见 `Wifi/Private.h:69` 的 `0x3F101000` 同类映射） |
| 2 | GPIO40/41 怎么切 PWM？ | **GPIO40/41 都设 ALT0**（=4）：GPFSEL4 的 pin40=bits[2:0]、pin41=bits[5:3] | `bcm2837-rpi-3-b.dts:121-125`：`&pwm { pinctrl-0 = <&pwm0_gpio40 &pwm1_gpio41>; status="okay"; }`；引脚功能值见 BCM2835 ARM Peripherals §6.2（GPIO40 ALT0=PWM0、GPIO41 ALT0=PWM1）。**不复用 `HalGpio`**（它只有 IN/OUT，音频脚不进 GPIO 白名单） |
| 3 | 时钟使能？ | **CM_PWMCTL `0x3F1010A0` / CM_PWMDIV `0x3F1010A4`**；源 = 19.2 MHz 晶振，MASH 可分频；目标 **`assigned-clock-rates = <10000000>`（10 MHz）** | 上游 `bcm283x.dtsi:416-418`：`clocks=<&clocks BCM2835_CLOCK_PWM>; assigned-clock-rates=<10000000>;`；`clk-bcm2835.c:84-85` `CM_PWMCTL=0x0a0` / `CM_PWMDIV=0x0a4`；CM 基址与本仓同窗（`Wifi/Private.h:69` `0x3F101000` + PASSWORD `0x5A000000`）。**评审纠**：勿用 50 MHz |
| 4 | 最小可闻算法？ | **方波**：RNG1=周期、DAT1=50% 占空比，循环改 DAT1 以 1 kHz 开关；**pi-1 不上 DMA** | BCM2835 §9.5（PWM 寄存器：RNG1 0x10 / DAT1 0x14 / CTL 0x00）；X64 标杆的 `HalAudioBeep` 语义只需"响"（`Archive/过程/柱归档/声卡驱动-HDA.md`） |
| 5 | PlayPcm 格式？ | API 层**对齐壳：48 kHz / 16-bit / 1–2ch**；**pi-1 只做 beep**，真 PCM 留 pi-1b | `Services/Shell/Commands/Audio.c`（WAV 约束）；`Include/Hal/HalDevices.h`（`HalAudioPlayPcm` 语义） |
| 6 | 拟建文件名？ | `Audio/PwmClock.c`、`Audio/PwmTransport.c`、`Audio/WavePlayback.c`、`Audio/BringUp.c`（均 ≤300） | 本文 §3.2 命名约定 |

### 6.2 Duo 列（audio-eyes-0 已填）

| # | 问题 | 结论 | 证据（文件:行） |
|---|---|---|---|
| 1 | TX I2S 哪路？ | **I2S3 @ `0x04130000`**（窗 0x2000），`capability="tx"`，**注释原文「I2S3 connect to internal DAC as TX」**；`dmas=<&dmac 7 1 1>`（写通道）、`mclk_out="true"` | `build/boards/default/dts/cv181x/cv181x_base.dtsi:612-620` |
| 2 | DAC 基址？ | **`0x0300A000`**（窗 0x100），`compatible="cvitek,cv182xadac"`，`clocks=<&i2s_mclk 0>` | 同文件 `:632-636`；寄存器偏移见 `linux_5.10/sound/soc/codecs/cv181xadac.h:18-29`（`AUDIO_PHY_TXDAC_CTRL0/1`、`AFE0-2`、`CLK`、`ANA0-3`、`TEST`，0x00–0x30） |
| 3 | 时钟 24.576 MHz？ | **是**：`audio_clock` fixed `clock-frequency=<24576000>`（#if 1 分支）；I2S/DAC 取 `i2s_mclk` | 同文件 `:901-907` |
| 4 | mute-gpio？ | **板级已删**：`&dac { /delete-property/ mute-gpio-l; /delete-property/ mute-gpio-r; }` ⇒ **无外置 mute 脚**，孔常通（`audio-duo-1` 现场再确认） | `build/boards/cv181x/sg2000_milkv_duos_musl_riscv64_sd/dts_riscv/sg2000_milkv_duos_musl_riscv64_sd.dts:51-54` |
| 5 | 最小送数：CPU vs DMA？ | **duo-1 先 CPU 轮询写 I2S3 TX FIFO**（蜂鸣即可）；SDMA 留后面 | dts 只声明 `dmas`（写通道）并不强制；对表厂商驱动 `sound/soc/cvitek/cv1835_i2s.c` 的 FIFO 写法 |
| 6 | 拟建文件名？ | `Audio/I2sTransmit.c`、`Audio/DigitalToAnalog.c`、`Audio/WavePlayback.c`、`Audio/BringUp.c`（均 ≤300） | 本文 §3.2 |

**眼睛刀结论一句话**：Pi = **PWM@0x3F20C000 + GPIO40/41 ALT0 + CM_PWM 10 MHz**（方波即可响）；
Duo = **I2S3@0x04130000 TX → DAC@0x0300A000 + audio_clock 24.576 MHz**（无 mute 脚，CPU 写 FIFO 即可响）。

---

## 7 · 禁止与评审红线

### 禁止

- 空 `HalAudioProbe!=0` 却无硬件路径  
- 改 `Wifi*` / `Bluetooth/**` / Docs/25 netif「顺便」  
- Duo 误跑 `ck-pi-sd`；只拷 elf 不更新 `boot.sd` 当部署完成  
- 抢壳 UART / BT UART / WiFi 电源脚  
- 单文件 >300；禁缩写顶全称  
- 未口令 commit（DS）；Cursor 仅刀绿自动 TG+TS  

### Cursor 评审勾选（audio-eyes-0）

- [x] Probe/PlayPcm — 本刀无行为，不适用  
- [x] 对表 dts/驱动（Duo 齐；Pi 时钟已纠回 10 MHz）  
- [x] VirtMin — 本刀不做  
- [x] 人耳 — 本刀不做  
- [x] 命名拟建文件全称  
- [x] **给 DS：TG+TS → JX `audio-pi-1`**  

---

## 8 · JX 提示词（可直接贴）

```text
JX
只做 Docs/29 ★ = audio-eyes-0
权威：Docs/29 §2+§6；路线图1102 §C
填 Pi/Duo 对表表；无行为代码（或仅 Audio: eyes ok）
禁改 Wifi/BT；禁扫寄存器
```

```text
JX
只做 Docs/29 ★ = audio-pi-1
HalAudio* + rpi-3b/Audio；beep 耳机可闻
禁抢 mini-UART / BT 脚
```

```text
JX
只做 Docs/29 ★ = audio-duo-1
I2S3+DAC@0x0300A000；beep 耳机可闻
禁改 Wifi*；部署 boot.sd
```

```text
JX
只做 Docs/29 ★ = audio-shell-1
VirtMin 注册 play/beep；help 可见；三架构编过
```

---

## 9 · 参考索引

| 文档 / 代码 | 用途 |
|---|---|
| `Include/Hal/HalDevices.h` | `HalAudio*` |
| `HAL/X64/Drivers/Hda/` + `声卡驱动-HDA.md` | 标杆语义 |
| `Services/Shell/Commands/Audio.c` | `play`/`beep` |
| `bcm283x.dtsi` / `bcm2837-rpi-3-b.dts` | Pi PWM |
| `cv181x_base.dtsi` i2s3 / dac@0300A000 | Duo 通路 |
| `linux …/sound/soc/cvitek/cv181xdac.c` | Duo 厂商序 |
| Docs/24/25 | 执行包体例与协作闭环 |

---

## 10 · 一句话

> **先眼睛钉 PWM / I2S3+DAC，再板级接 `HalAudio*`，最后 VirtMin 挂 `play`——判据是耳机孔里真有声，不是日志漂亮。**
