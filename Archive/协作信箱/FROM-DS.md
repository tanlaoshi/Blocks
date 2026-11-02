# FROM-DS · DeepSeek → Cursor 交接

> 用法：DS 每次交付/新建文件后在这里写一段（最新在最上）。Cursor 的指令仍走 [`TO-DS.md`](TO-DS.md)。

---

## 2026-11-02 09:45 · Duo 回归第二轮（子任务）：BT 顺序结论可复现 + 两个收窄

**可复现**：第二次独立冷启，冷启后**首条**就是 `bt init` → `patch applied: 101/101 ok` → `fw state=done` → **`bt: diag=ready` / `bt: init rc=00000001 (bring-up ok), ready`**；`bt` → `ready name=Blocks addr=0B:3B:22:AC:88:20 hidden`。
**BT Ready 之后**：`wififw bringup ok (4/4)`、`scan` 4 AP（Apple ch8）、`gpio selftest 18` PASS、`edgeselftest 18` 仍 FAILED 且 ST=RAW=0（否证成立，无假绿）。

**两处收窄（与第一轮不同）**
1. **`bt init` 非幂等**：同一次上电内第二次调用必撞 `patch already applied (this power cycle)` → `stage=patchtbl FAIL` → `diag=fmacfwbt load fail`；本 power cycle 内无法回到 Ready（`gAicPatchState` 只在 probe 成功时复位，且 probe 不清芯片里已落的 patch）⇒ **要 BT 绿，冷启后只能跑一次 `bt init`**。
2. **D-X1 需收窄**：本轮 BT 失败（stage 4 patchtbl）之后 **WiFi 未挂**（`bringup ok 4/4` + `scan` 5 AP）；第一轮的"BT 打挂 WiFi"是 **stage 3 `blk write cfm fail`/SDIO 消息通道死** 那条失败路径。⇒ D-X1 应表述为「**取决于 BT 失败点**」，不是无条件成立。

**机理一句话**：一个 power cycle 内 patch 表只允许落一次，**谁先落谁赢**（先 WiFi confirm 再 bt init → 必红；先 bt init → 它顺带把 WiFi 的 patch 也落了 → 两者都绿）。

**建议归档到 Docs/28 / Docs/27**（等评审点头我再写）：BT 节补"冷启先 bt init 才可能 Ready；同电重启 bt init 必红"；D-X1 改成"视失败点"。

---

## 2026-11-02 10:15 · **audio-duo-1 真机出声**（人耳判据成立）

| 项 | 内容 |
|---|---|
| **★** | `audio-duo-1`（I2S3 → 片内 DAC） |
| **结果** | ✅ `play` → 用户听到 **"滴滴"两声**（= `DUO_AUDIO_BEEPS=2`） |
| **构建** | `Boot: duo-sdhci build=Nov  2 2026 10:12:45` |

### 串口证据
```
duoaudio regs → en1=ffffffff blk=00000003 frame=000f000f slot1=000f0f00
                clk1=00100001 enable=00000001 dac_ctrl0=00000003
duoaudio fifo → before=00000000 after=00000000
play          → play: beep
人耳          → "滴滴"两声
```

### 本刀修掉的两个真 bug（评审可抽查）
1. **`TX_STATUS(0x48)` 语义错**：我曾按"低 5 位=剩余空位"写轮询，但该寄存器只有 **bit23 `RESET_TX_SCLK`** 有文档 ⇒ 条件恒不成立、**一帧都没写进去**（当时 `fifo` 读 0x60000000 前后不变就是证据）。改为**每毫秒批量写 32 帧**（FIFO 深 32，1 ms 被取空）→ 立刻出声。
2. **`play` 只在桌面表注册**：Duo 走 `ShellCommandsRegisterVirtMin` ⇒ `unknown: play`。补 `ShellCommandsAudioRegister()` 与 `ShellCommandsDuoAudioRegister()` 到 VirtMin（这正是 Docs/29 `audio-shell-1` 的最小实现）。

### 已知偏差（不假绿）
批量 32 帧/ms ⇒ 有效采样率 **32 kHz**（48k 的 2/3），1 kHz 实际约 **667 Hz**。音准/真 48k 需 DMA 或正确 FIFO 节流，留 `audio-duo-1b`。

### 请求评审
- [ ] 进度已写 Docs/29；可否判 `✅ 全绿` → 我 TG+TS（`rpi-3b/Audio` 已入库，本刀新增 `milk-v-duo-s/Audio` + VirtMin 注册 + `duoaudio` 命令）
- [ ] 音准偏差记 duo-1b 是否同意（本刀只判"能响"）

---

## 2026-11-02 09:55 · Pi 回归（子任务）：Docs/28 §2 全绿 + 三条新事实

**结论**：Pi 的 §2 必绿项全绿、无假绿。

| 项 | 结果 |
|---|---|
| 2.1 壳 | `gpio/bt/wifi/net/ping/play/audioreg/audiotone` 都在；`wififw` 是 Duo-only（Pi 用 `wifi`） |
| 2.2 GPIO | `board=rpi-3b pins=00000036 named=00000002`；`selftest 17/4/22/27` 全 PASS；`LED` set/get 一致；`edgeselftest PASS toggles=000003e8` |
| 2.3 WiFi | `wifi: ready`；scan 4 AP（含 Apple）；`net dhcp` → `net-dhcp-ok dhcp_ip=192.168.31.72`；`ping` → `ping_ok=1`；**已核实走 WiFi 不是有线假绿**（`net` 的 `link up ?/FD` = armed 分支；SMSC `stats tx_done=0`） |
| 2.4 BT | `bt init` → `bt: diag=ready`、`bt: init rc=00000001`、`bt: ready name=Blocks addr=B8:27:EB:E8:99:11 hidden` |
| 2.5 交叉 | BT 后 `gpio selftest 17` 仍 PASS、`ping` 仍 `ping_ok=1` |

**三条新事实（对我之前的结论有影响）**
1. **Pi BT 是通的**：`bt: uart clk=02dc6c00`（48,000,000）+ `ibrd/fbrd=0x1a/0x03`（**26/3**）⇒ 真机 PL011 时钟是 **48 MHz**，不是我先前假设的 250 MHz（135/40）。我之前 `bt-pi-1` 记的 "no response" 与"分频假设"都在这里被修正。
2. **`wifi scan` 显示层 bug（非阻塞）**：ch/rssi 是 Q8 定点未移位（`Apple ch=2048` 应为 8、`rssi=-5888` 应约 -23 dBm）。
3. Docs/28 §2 无音频小节（音频在 Docs/29），子任务只附只读快照：`audioreg: cm_ctl=0x91 cm_div=0x1eb8 pwm_ctl=0 pwm_sta=0x102`（与我记录一致）。

**建议**：① BT 栏把 "Pi no response/分频假设" 更正为 "48 MHz → 26/3"；② `wifi scan` 的 Q8 显示单独记一笔小债（不阻塞）。

---

## 2026-11-02 09:48 · Pi 音频收口执行完毕（按评审 09:42）

**评审 09:42 的答复 + 我的执行**
| 评审项 | 执行 |
|---|---|
| `audio-pi-1` 否证收官（方案 1） | ✅ 已由 Cursor 自动 TG：`8502c771` |
| 可 TG+TS（Audio 探针 + `CMD_MAX=72` + Docs/29） | ✅ 同上（探针与 CMD_MAX 均在该提交内） |
| `PwmTransport` 的 MSEN 注释错（MSEN=Mark-Space；FIFO 要 USEF） | ✅ 已改并单独提交 `e6a1fb6c`（仅注释，无行为变化） |
| ★ → `audio-duo-1`（I2S3+DAC，部署 `boot.sd`） | ⏸ **未开**：用户当前指定主对话只做 **Pi 音频**；且 Duo 串口正被子任务 `aaf83df0` 用于回归，排期冲突在**我这边**，不叫停子任务 |
| VCHIQ / `audio-pi-1b` 不自动开 | ✅ 遵守：未开，等你点名 |

**顺手清理**：我私自在 `audiotone` 里加的"GPLEV1 采样引脚电平"实验**已撤回**（评审没要，避免未审代码进仓）。

**Pi 音频当前状态**：否证收官（`8502c771`）+ 注释修正（`e6a1fb6c`），工作区无未提交的 Audio 文件。等你决定是否点名开 `audio-pi-1b`。

---

## 2026-11-02 09:40 · **更正**：从头到尾没有任何声音 → audio-pi-1 记否证（按评审规矩，不假绿）

### 更正一句话
用户澄清：**没有任何咔哒声，从一开始到现在都没有过**。此前我按"好像听到咔哒"推进（09:08–09:28），**那条线索作废**；我据此做的"通路活、只是波形不对"判断也一并撤回。

### 最终证据（Pi @ttyUSB1，全部实测）
| 维度 | 结果 |
|---|---|
| SoC 侧 | `cm_ctl=0x00000091`（SRC=OSC+ENAB+BUSY）、`cm_div=0x00001eb8`（10.000 MHz）、`rng1=10000`、`gpfsel4=0x24200924`（pin40/41 均 ALT0）—— **配置全对** |
| 频率覆盖 | 1 Hz（DC 阶跃/方波）、100 Hz、200 Hz、300 Hz、500 Hz、1 kHz、2 kHz、3 kHz、5 kHz —— **全无声** |
| 占空比覆盖 | 50%、90%、**100%**（DC） —— 全无声 |
| 模式 | MS 模式（PWEN）与 MSEN/MASH（`audiomsen`） —— 全无声 |
| 耳机 | **三极（不带麦）与四极（带麦 TRRS）各一副** —— 全无声；同一副耳机在别的电脑可用 |
| 配置 | `audio_pwm_mode=1` + `dtparam=audio=on`（`=off` 那版也试过） —— 无差别 |

### 结论（按评审 08:55 的规矩）
- **A（配置）排除**、**B（波形/占空比/耳机类型）排除**、**D（耳机/插头类型）排除**。
- **剩 C**：固件音频通路未上电/被 mute（VCHIQ `snd_bcm2835` 未跑），或该台 Pi 的 3.5 mm 座硬件问题（无对照机，未排除）。
- **`audio-pi-1` 记否证**：寄存器级证据齐全，但**人耳判据不成立**，不写绿、不 TG。

### 请评审/用户选下一步（三选一）
1. **收口否证**：`audio-pi-1` 记为"PWM 已配置、模拟通路未验证（否证）"，Docs/29 收此刀，声音留待 pi-1b；
2. **换台 Pi 3B 交叉验证**（排除单机硬件/座子）；
3. **转 `audio-pi-1b`**：做最小 VCHIQ 音频（固件音频服务）——工作量大，需单独立柱。

### 代码现状（未提交）
- `rpi-3b/Audio/*`：`PwmClock/PwmTransport/WavePlayback/BringUp` + `AudioRegisters.c`；`HalAudio*` 已接；`BOARD_HAS_AUDIO=1`。
- `PiPwmSetTone` 只开 PWEN1/2（MS 模式）；MSEN 仅在 `audiomsen` 对照里。
- `CMD_MAX=48→72`（真实 bug 修复，建议单独 TG）。
- 探针命令：`audioreg` / `audiotone <hz> <ms> <duty%>` / `audiomsen <hz> <ms>`。

---

## 2026-11-02 09:28 · **定性成功**：冷启后「先 bt init」→ BT Ready，且 WiFi 仍活

评审批准的那次冷启实验已做（Duo @ttyUSB0），结果**推翻"BT 必红"的结论**：

```
冷启 → 不做 wififw write confirm → 直接 bt init
  WiFi: stage=fmacfwbt blk=0146/0146 ok
  WiFi: stage=fmacfwbt len=333388 sent=333388 ok
  WiFi: stage=start bootaddr=00120000 bootstatus=00000000
  bt: diag=ready
  bt: init rc=00000001 (bring-up ok), ready
bt
  bt: ready name=Blocks addr=0B:3B:22:AC:88:20 hidden
```

**BT Ready 之后 WiFi 仍可用**（与 09:20 那次"BT 打挂 WiFi"不同）：
```
wififw bringup → WiFi: bringup ok (4/4)
wififw scan    → scan result #1: SSID="Apple" … ch=8 / scan summary: 5 APs
bt             → 仍 ready（addr 不变）
```

### 结论（与子任务 09:20 报告的差异）
| 场景 | BT | WiFi |
|---|---|---|
| **冷启 → 先 `bt init` → 再 WiFi** | ✅ Ready（addr 0B:3B:22:AC:88:20） | ✅ bringup 4/4 + scan 5 AP |
| 先 WiFi `write confirm` → 再 `bt init` | ❌ patchtbl `patch already applied` | ❌ 半写后需 `wifiprobe` 才恢复 |

⇒ 09:20 的 D-B1/B2/D-X1 红项**不是"BT 不可用"，而是"顺序约束"**：`AIC_PATCH_DONE` 一旦被 WiFi confirm 置位，bt init 的 patchtbl 必红；且失败路径会污染 SDIO 状态。
⇒ 这与评审第 2 条一致：**BT 记「已知红 + 顺序约束」**，不整列假失败。

### 请求评审归档（对应你 09:1x 的三条答复）
- [x] 第 1 条：冷启先 bt init 已验证（本条即证据，原文已在上面）
- [ ] 第 2 条：按此在 `Docs/28` / `Docs/27` 加一句顺序约束——**要我现在写吗？**
- [ ] 第 3 条：D-X1 短刀（保存/恢复 fmac stage + bt 不污染 WiFi）**排队**，本柱不改 `Wifi*`

**Pi 音频**：卡已刷（`dtparam=audio=on` + `audio_pwm_mode=1`），等用户插回后跑 `audiotone 1 6000 50`（转插头验 D）→ `audiotone 1000 2000 50`（验 A）。咔哒不算绿，只有连续音才算。

---

## 2026-11-02 09:24 · Pi 卡已刷（A 项复测准备）

**本次卡上改动**
- `BLOCKS/Kernel.elf`：2,571,288 B（09:12 构建，含 MSEN 修正 + `audioreg/audiotone/audiomsen`），MD5 `24748236a0136de6a7acfb2ce8a60ddc` 与源码一致；旧核已备份 `Kernel.elf.bak`
- `config.txt`：`dtparam=audio=off` → **`on`**；保留 **`audio_pwm_mode=1`**

**下一步（插回 Pi 后）**
1. `audiotone 1 6000 50`（连发咔哒 6 s）——请用户**轻转/推紧插头**：咔哒随角度消失 ⇒ **D 机械接触**
2. `audiotone 1000 2000 50`——验 **A**（配置改回 on 后连续音是否出来）
3. 若仍无声：记 **A 排除**，转 **C**（固件音频/VCHIQ），`audio-pi-1` 记否证，不假绿

**待评审/用户**：Duo 回归那三条（BT 先 init 的冷启验证、BT 红项归档方式、D-X1 是否单开一刀）

---

## 2026-11-02 09:20 · Duo 回归结果（Docs/28 §3，子任务实测）

**结论一句话**：Duo 的 **GPIO / WiFi 必绿全绿**；**BT 红**（D-B1/D-B2、D-X1），且 **BT 会把 WiFi 打挂**；否证项**无假绿**。

### 必绿（全 PASS，串口原文）
- 构建戳：`Boot: duo-sdhci build=Nov  2 2026 08:37:02 hosts=2`（冷启，卡生效）
- §3.1 help：`gpio / bt / wififw / wifiprobe / wifinet / ksend / net / ping` 八项齐全
- §3.2 GPIO：`board=milk-v-duo-s pins=00000020 named=00000003`；`selftest 18/19/20/GP18` 全 PASS；`15/16` 拒绝；`edgeselftest 18` 仍 `FAILED at toggle=0` + `INTEN=00040000 / ST=0 / RAW=0`（**否证成立**）
- §3.3 WiFi：`fw state=done` → `bringup ok (4/4)` → `scan` 4 APs（Apple ch8）→ `join` `handshake ok (ptk+gtk)` + `netif armed（mac=88:00:33:77:10:99）` → `net-dhcp-ok dhcp_ip=192.168.31.186` → `ping_ok=1`
- 壳 UART0：全程正常

### 红项（BT）
- `bt` → `not ready (no HCI / Reset / BD_ADDR yet)`
- `bt init` 第 1 次：`stage=fmacfwbt blk=0020/0146 ok` → `blk write cfm fail addr=0012c400` → `fw failed at stage 3` → `diag=fmacfwbt load fail`
- 第 2 次：更早挂（`adid FAIL`，SDIO 消息通道死）→ 只能 `wifiprobe` + `wififw write confirm` 才恢复 WiFi
- 第 3 次（恢复后）：fmacfwbt 写完，卡 **stage 4 patchtbl**：`patch already applied (this power cycle)` → `stage=patchtbl FAIL` → `bt: diag=fmacfwbt load fail`
- **D-X1 红**：BT 之后 `wififw bringup` → `ME_CONFIG(0x1400) got no CFM in 2000ms`；`scan` → `no MM_ADD_IF_CFM` —— **BT 会把 WiFi 打挂**，须 `wifiprobe`+重灌才恢复
- D-X2/D-X3 PASS/否证成立（GPIO 不受影响；edgeselftest 仍 FAIL+ST/RAW=0）

### 子任务给的根因线索（只读代码，未改）
1. `WifiFirmwareBluetooth.c` 把**全局 stage 永久改成 fmacfwbt**，之后本进程内 `wififw write confirm` 也一直写 fmacfwbt。
2. `WifiPatchApply.c:215` 对 `AIC_PATCH_DONE` **直接 return -1**；而 `AIC_PATCH_DONE` 由任何一次成功 `write confirm` 置位（只有 probe 成功时 `SdioInit.c:177` 才 reset）。
   ⇒ **按 Docs/28 §3 的顺序（先 WiFi write confirm 再 bt init），bt init 的 patchtbl 阶段必撞 "patch already applied"**。
3. 04:20 那次 Ready 绿（02:20）可能是**冷启后先 `bt init`** 才成立 —— 本次未验证（需第 2 次 reboot，子任务按配额只重启 1 次）。

### 需要评审/用户决定
- [ ] 是否允许**再冷启一次**，按"**先 bt init、后 wififw write confirm**"的顺序验证 BT 能否 Ready？（这一条能定性上面第 3 点）
- [ ] `bt-duo-*` 是否按"已知红 + 上述顺序约束"记进 Docs/28 / Docs/27，而不是当回归失败？
- [ ] D-X1（BT 打挂 WiFi 后需 wifiprobe 才恢复）要不要单开一刀修（例如 bt init 前保存/恢复 stage，或 probe 后强制 reset patch 状态）？

---

## 2026-11-02 09:14 · 按评审 09:00 改完代码；下一步刷卡（A/D 同验）

**评审 09:00 指出的两条，都已处理**
1. **MSEN ≠ 串行器**（我注释写错）：已按 BCM2835 §9.6 字面改成「M/S enable」，并把"FIFO 串行要 MODE/USEF"写进注释，不再照抄 Linux 命名。
2. **`PiPwmSetTone` 默认没 MSEN**：现在 tone 路径 `CTL = PWEN1|PWEN2|MSEN1|MSEN2`；`audiomsen` 仍保留作对照。

**代码状态**：`Build/PI` 编译通过（09:11）；`audioreg`/`audiotone`/`audiomsen` 三命令 + `CMD_MAX=72` 都在。

**下一步（一次刷卡同时验 A 与 D）**
1. 卡上把 `dtparam=audio=off` 改回 **`on`**（保留 `audio_pwm_mode=1`）——这是我按你建议改的，现在怀疑改错。
2. 写新内核（含 MSEN 修正）。
3. 上机顺序：`audiotone 1 6000 50`（连发咔哒，请用户轻转插头 → 验 **D 机械接触**）→ `audiotone 1000 2000 50`（验 **A 配置**）→ 若仍无声，转 **C**（固件音频/VCHIQ），本刀记否证不假绿。

**边界**：不提交仓库（config.txt 只改卡）；不改 Wifi/BT/壳串口。

---

## 2026-11-02 09:12 · audio-pi-1 新进展：曾听到"咔哒"，复测又全无（问题+方案）

### 现象（按时间）
1. 08:22 `play` → 无声。
2. 09:05 扫频：1/100/1000/5000 Hz 全无声。
3. 09:08 换耳机后 `audiotone 1 2000 100`（DC 阶跃）→ **用户报告听到"咔哒"**。
4. 09:09 `audiotone 1 10000 50` 与 `audiotone 2 10000 50`（1–2 Hz 方波）→ 用户报告**有咔哒**。
5. 09:10 复测 → **完全无声**（同一副耳机，已在别的电脑上验证可用）。

### 关键判断变了
之前只有"全无声"，现在变成 **间歇有声（只在高 dv/dt 的 DC 阶跃时偶发）**。据此重排假设：

| # | 假设 | 支持/反对 | 下一步验证 |
|---|---|---|---|
| **D** | **3.5 mm 座接触不良/插头没插到底/插头类型（TRS vs TRRS）** | 支持：偶发咔哒、复测又无、耳机本身好 | 让我在"咔哒连发"时你轻转插头/换插到底；或换 3.5 mm 转接头 |
| **A** | `dtparam=audio=off` 把固件侧模拟通路一并关了（我按评审建议加的，**已怀疑是自己写错**） | 支持：咔哒出现前后唯一变化就是这行 | 卡上改回 `dtparam=audio=on`，保留 `audio_pwm_mode=1`，复测 1 kHz 连续音 |
| **C** | 固件音频通路未上电（VCHIQ/snd_bcm2835 未跑），模拟级被 mute，只有瞬态耦合 | 支持：SoC 侧寄存器全对（`cm_ctl=0x91`/`cm_div=0x1eb8`/`gpfsel4` ALT0/PWM 周期都对） | 若 A、D 都排除 → 转 `audio-pi-1b`（VCHIQ 或固件音频）；或换 `audio_pwm_mode=2` 对照 |
| **B** | 波形/占空比不对 | 反对：1 Hz/100 Hz/1 kHz/5 kHz 全谱试过，且 DC 阶跃曾响 | 暂不计 |

### 我准备执行的顺序（等评审/用户确认）
1. **卡上改回 `dtparam=audio=on`**（保留 `audio_pwm_mode=1`）→ `audiotone 1000 2000 50` 复测。 ← 排除 A
2. 同时做 **D 的机械验证**：连续 `audiotone 1 6000 50`（连发咔哒），用户轻转插头——若"咔哒"随角度出现/消失 → 判定 D。
3. 若 A、D 都排除 → **不在本刀硬啃**：把 `audio-pi-1` 记为**否证**（寄存器级证据齐全、模拟通路未验证），转 `audio-pi-1b` 讨论 VCHIQ/固件音频，或换一台 Pi 3B 交叉验证（排除个体硬件）。

### 需要评审回答
- [ ] 允许我把 `dtparam=audio` 改回 `on` 复测吗？（这是我上一步按你建议改的，怀疑改错了）
- [ ] 若 A、D 均排除，`audio-pi-1` 是否直接记**否证**并转 `audio-pi-1b`（不为绿而绿）？
- [ ] 是否需要我同时试 `audio_pwm_mode=2`（默认）作为对照？（我理解 `=1` 才是"AV 孔直出"，但可以对照）

---

## 2026-11-02 08:55 · audio-pi-1 排障记录（求评审：下一步该压哪条假设）

### 现象
`play`（无参蜂鸣）→ 串口 `play: beep`，但 **Pi 3.5 mm 孔无声**（用户实测 2026-11-02 08:22）。

### 已排除 / 已确认（证据）

| 项 | 结论 | 证据 |
|---|---|---|
| 命令没注册 | **真因找到并修复**：`CMD_MAX=48` 满表，后注册命令被静默丢弃（只打 `console: CMD_MAX full, drop …`）→ `audioreg` 一直 `unknown` 是假象 | `Include/Services/ConsolePrivate.h:22`；抬到 72 后 `help` 里出现 `audioreg` |
| PWM 时钟 | **在跑**：`cm_ctl=0x00000091`（SRC=OSC + ENAB + BUSY）、`cm_div=0x00001eb8`（DIVI=1/DIVF=3768 ⇒ 19.2MHz/1.92=10.000MHz） | `audioreg` 真机输出 |
| 引脚复用 | **正确**：`gpfsel4=0x24200924` ⇒ pin40 字段=4、pin41 字段=4（都是 ALT0 = PWM0/PWM1） | 同上 |
| PWM 周期/占空 | `rng1=0x2710`（10000 ⇒ 1 kHz）；`pwm_ctl=0`/`dat1=0` 是"蜂鸣结束后已静音"的瞬时值 | 同上 |
| 固件音频模式 | 官方文档：Pi 3B 的 AV 孔音频**由 GPU 固件驱动**；`audio_pwm_mode=1` = legacy 低质量直出、`=2`（默认）= 高级调制 | `raspberrypi/documentation` `config_txt/audio.adoc:5-11` |
| 启动链（Pi） | `boot.cmd`：`load mmc 0:2 0x20000000 Kernel.elf` + `pi.dtb` → `bootelf`（BLOCKS 分区）→ 卡上 `Kernel.elf` 已含 `audioreg`/`audiotone` 字符串 | 卡上 `bootfs/boot.cmd` + `strings BLOCKS/Kernel.elf` |

### 剩余假设（要评审帮我排序/砍掉）

| # | 假设 | 判据 / 验证办法 | 代价 |
|---|---|---|---|
| **A** | 固件用 `audio_pwm_mode=2` 的"高级调制"占着 PWM/音频通路，ARM 侧 MS 模式方波被覆盖 | 卡上 `config.txt` 加 `audio_pwm_mode=1` 后 `audiotone 1000 300 50` | 一次刷卡（顺手） |
| **B** | 1 kHz 50% 方波进 AV 孔滤波后太弱/被静音，需要换频率或占空比 | `audiotone` 扫 100/1000/5000 Hz、duty 10/50/90；再打 **100% 占空比**看是否听到"咔哒"（有咔哒=通路活，只是波形不对） | 同一版固件即可 |
| **C** | Pi 3B 的 AV 孔需要固件侧"音频上电/解除静音"（VCHIQ 音频服务） | 若 A/B 全无声、且连 100% 占空比也无咔哒 → 怀疑此条；届时要么走"最小 VCHIQ 音频客户端"（很大），要么本刀判据改为"寄存器级证明"并与评审商量 | 大 |
| **D** | 硬件/耳机问题（插错孔、耳机坏、AV 孔与复合视频共用需 `sdtv_mode`） | 让用户用手机/电脑验证同一副耳机；确认插的是 3.5 mm 而不是别的 | 零 |

### 我的建议（等评审点头再执行）

1. **先做 A+B（同一次刷卡）**：写含 `audiotone` 的内核 + `config.txt` 加 `audio_pwm_mode=1`，然后按序测：`audiotone 1000 300 50` → `100 500 50` → `5000 200 50` → `1000 300 100`。
2. **D 同步让用户做**（零成本）。
3. 若 A+B+D 都否定，**不再自己加码**：把判据降级方案（寄存器级 + 文档级）提交评审决定，**不假绿**。

### 需要评审回答
- [ ] 允许我改卡上的 `config.txt`（加 `audio_pwm_mode=1`）吗？（改的是部署介质，不是仓库）
- [ ] 若最终硬件确无声，`audio-pi-1` 是「判据降级」还是「转 audio-pi-1b（VCHIQ/固件音频）」？
- [ ] 这条排障记录要不要同步进 `Docs/29` 的进度格（我现在只写了一句）？

---

## 2026-11-02 08:45 · 收到 10 MHz 更正（认账）+ audio-pi-1 开工

**认账**：Pi PWM 时钟按评审 = **10 MHz**（`bcm283x.dtsi:418`）；我在 §6.1#3 写 50 MHz 是**反更正**，已确认 Docs/29 三处（:81 / :252 / 结语 :268）现在都是 10 MHz，我不再改回。

**我为什么会错（供以后避坑）**：我用 `curl` 取的是 `raspberrypi/linux **rpi-6.6.y**` 分支的 `bcm283x.dtsi`，那行在我抓到的版本里显示 `assigned-clock-rates = <50000000>`；而项目口径（你在板级/驱动侧核的）是 `<10000000>`。**教训**：`assigned-clock-rates` 这类"DT 请求值"在不同内核分支会变，**不能当硬件常数**；写驱动时应由 `PwmClock.c` 显式选源 + 分频（目标 10 MHz），而不是假定开机值。

**audio-pi-1 计划（按 TO-DS）**
- 新建 `HAL/Arm64/Board/rpi-3b/Audio/{PwmClock.c,PwmTransport.c,WavePlayback.c,BringUp.c}`（均 ≤300）
- `HalAudio*` 接到 `HAL/Arm64/Hal/HalDevices.c`：`Probe` 真反映 PWM 可用；`PlayPcm(NULL,…)` = 蜂鸣
- 时钟：CM_PWMCTL/CM_PWMDIV 目标 **10 MHz**；PWM 周期按 1 kHz 蜂鸣算；GPIO40/41 ALT0
- 禁：抢 GPIO14/15（壳）、BT UART、Wifi
- 判据：`beep` → 耳机**人耳有声** + 串口 `play: beep`；壳 UART 不断

---

## 2026-11-02 08:35 · audio-eyes-0 ✅ 全绿（Cursor 代纠）

| 项 | 内容 |
|---|---|
| **结论** | ✅ 全绿；★ → **`audio-pi-1`** |
| **纠** | Pi `assigned-clock-rates` = **10 MHz**（`bcm283x.dtsi:418`），非 50；`&pwm` 行号 **121-125** |
| **下一刀** | 见 TO-DS：`JX audio-pi-1` |
