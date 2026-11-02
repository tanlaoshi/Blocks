# 28 · Pi / Duo · WiFi · BT · GPIO 全面回归计划

> **用途**：真机手测总表（2026-11-02 柱收官后）。照本节跑完 = WiFi / BT / GPIO 现网基线仍绿（含已钉否证）。  
> **串口现状（可改）**：Duo `@ttyUSB0` · Pi `@ttyUSB1` · 115200 8N1。换线先确认 `Blocks>`。  
> **参照**：Docs/24（BT）、Docs/25（Duo WiFi）、Docs/26–27（眼睛）、路线图 1102（GPIO）、1031（Pi WiFi）。

---

## 0 · 开测前

| 项 | 要求 |
|---|---|
| 固件 | 与 `origin/main` 对齐后刷卡 / ksend；冷启看 build 戳（Duo：`Boot: duo-sdhci build=`） |
| Duo 部署 | 冷启走 **FIT `boot.sd`**：`ksend … --path BOOT:boot.sd` 或读卡器写 `boot`；**仅拷 `Kernel.elf` 不够** |
| Pi 部署 | `ksend … --path BLOCKS:Kernel.elf`（或 CK `ck-pi-sd`）；**禁止对 Duo 跑 `ck-pi-sd`** |
| 课网 | SSID **`Apple`** + 课密；网关 **`192.168.31.1`**（与 Docs/25 一致） |
| Assets | Duo：`/Assets/Fw/aic8800/` 含 `fmacfw.bin` **与** `fmacfwbt_8800d80_u02.bin`；Pi：`BCM43430A1.hcd` 在固件约定路径 |
| 顺序建议 | **每板**：GPIO（快）→ WiFi → BT（Duo BT 会重灌 fmacfwbt，数分钟）→ **再扫一眼 GPIO/WiFi**（交叉回归） |
| 记录 | 每节贴 **串口原文**（或 grep 命中行）；否证项只验「仍失败且 Ready/假绿未撒谎」 |

**总通过口径**：下表「必绿」全勾；「否证」项行为与钉死结论一致即算通过。

---

## 1 · 矩阵（一眼）

| 能力 | Pi 必绿 | Duo 必绿 | Duo 否证（须仍成立） |
|---|---|---|---|
| GPIO list / selftest | ✅ | ✅ 白名单 18/19/20 | — |
| GPIO 具名 | ✅ LED/BTN | ✅ GP18… | — |
| GPIO edgeselftest | ✅ 1000/1000 | — | ✅ 同脚仍 FAIL + ST/RAW=0 |
| WiFi 探卡 / 固件 | ✅（现网路径） | ✅ `write confirm`→done | — |
| WiFi scan / join | ✅ | ✅ | — |
| 通用网 IP + ping | ✅ 课网 | ✅ `net dhcp` + `ping`→`ping_ok=1` | — |
| BT Ready + 非零 MAC | ✅ `bt` | ✅ `bt init`→ready | — |
| 壳串口未抢 | ✅ mini-UART | ✅ UART0 | — |

---

## 2 · Pi 回归脚本

### 2.1 壳与部署

```text
# @ttyUSB1；冷启后应有 Blocks>
help          # 须有：gpio、bt、wifi（及现网 net/ping）
```

### 2.2 GPIO

```text
gpio list
# 期望：board=rpi-3b pins=00000036 named≥2；含 LED / BTN

gpio selftest 17
gpio selftest 4
gpio selftest 22
gpio selftest 27
# 期望：各行 gpio: selftest PASS …

gpio selftest LED
gpio set LED 1
gpio get LED          # =1
gpio set LED 0
gpio get LED          # =0

gpio edgeselftest 17
# 期望：gpio: edgeselftest PASS toggles=000003e8（默认 1000）
# 快测：gpio edgeselftest 17 50 → PASS toggles=00000032
```

| # | 项 | ☐ |
|---|---|---|
| P-G1 | list / selftest 数字脚 | |
| P-G2 | LED set/get | |
| P-G3 | edgeselftest PASS | |

### 2.3 WiFi（对表 1031 / 壳 `wifi`）

```text
wifi info                 # 或现网等价；须见模组/状态，不卡死
wifi scan                 # 期望：有 AP，课网 Apple 可见更佳
wifi join Apple <课密>    # 期望：join-ok / 关联成功类原文（对表现网日志）
# IP / ping（现网若走壳 net）：
net dhcp                  # 或 wifi 路径已自动拿址则跳过
ping 192.168.31.1         # 期望：reply / ping_ok=1（若已接通用标签）
wifi info                 # 期望：非零站点 MAC、有 IP 更佳
```

| # | 项 | ☐ |
|---|---|---|
| P-W1 | scan 有 AP | |
| P-W2 | join 课网成功 | |
| P-W3 | ping 网关通 | |

### 2.4 蓝牙

```text
bt
# 冷启后若未 ready：bt init
# 期望：bt: diag=ready
#       bt: ready name=Blocks addr=B8:27:EB:…（Pi OUI，非全 0）hidden
# 回归：壳串口仍可打字（未抢 mini-UART）
```

| # | 项 | ☐ |
|---|---|---|
| P-B1 | Ready + 非零 addr + name=Blocks | |
| P-B2 | 壳口不断 | |

### 2.5 Pi 交叉

```text
gpio selftest 17          # BT/WiFi 之后仍 PASS
ping 192.168.31.1         # BT 之后网仍通（若刚 join）
```

---

## 3 · Duo 回归脚本

### 3.1 壳与部署

```text
# @ttyUSB0；确认 Boot: duo-sdhci build=<今日戳>
help          # 须有：gpio、bt、wififw、wifiprobe、wifinet、ksend、net、ping
```

### 3.2 GPIO

```text
gpio list
# 期望：board=milk-v-duo-s pins=00000020 named=00000003
#       GP18 / GP19 / GP20

gpio selftest 18
gpio selftest 19
gpio selftest 20
gpio selftest GP18
# 期望：PASS

gpio selftest 15
gpio selftest 16
# 期望：拒绝 / FAILED（白名单外，防改串口脚）

gpio edgeselftest 18 8
# 否证期望：FAILED at toggle=00000000
# 若有 snap：INTEN 有 bit18，ST=0 RAW=0（与 1102 G5 钉死一致）
# ⚠ 不要为「变绿」改数据面或假 Ready；双脚跳线另开刀
```

| # | 项 | ☐ |
|---|---|---|
| D-G1 | list + selftest 18/19/20 | |
| D-G2 | 15/16 拒绝 | |
| D-G3 | edgeselftest 同脚仍否证 FAIL | |

### 3.3 WiFi → 通用网（Docs/25）

```text
wifiprobe                 # 可选；卡已在则可跳
wififw write confirm      # 期望：fw state=done (all stages ok)  — 数分钟级
wififw bringup            # 期望：bringup ok (4/4)
wififw scan               # 期望：summary 含 ≥1 AP（最好有 Apple）
wififw join Apple <课密>  # 期望：eapol handshake ok (ptk+gtk)；常自动 WiFi: netif armed
wifinet status            # 期望：armed=1；有 mac=…
net dhcp                  # 期望：net-dhcp-ok dhcp_ip=192.168.31.x
ping 192.168.31.1         # 期望：reply … 且 ping_ok=1
```

| # | 项 | ☐ |
|---|---|---|
| D-W1 | write confirm done | |
| D-W2 | bringup + scan | |
| D-W3 | join + armed | |
| D-W4 | dhcp + ping_ok=1 | |

### 3.4 蓝牙（Docs/24 bt-duo-2）

```text
bt
# 若 not ready：
bt init
# 期望过程含：
#   WiFi: fmac stage -> fmacfwbt_8800d80_u02.bin
#   stage=fmacfwbt … sent=333388 ok
#   fw state=done
#   bt: diag=ready
#   bt: init rc=00000001 (bring-up ok), ready
bt
# 期望：bt: ready name=Blocks addr=XX:…（非全 0）hidden
# 历史绿值参考：0B:3B:22:AC:88:20（以当次读数为准）
```

| # | 项 | ☐ |
|---|---|---|
| D-B1 | fmacfwbt 灌入 ok | |
| D-B2 | Ready + 非零 addr + Blocks | |
| D-B3 | 壳 UART0 仍可用 | |

### 3.5 Duo 交叉（重要）

`bt init` 会重跑 **fmacfwbt** write confirm，可能打乱「仅 fmacfw」假设；回归建议：

```text
# BT 之后：
wififw bringup
wififw scan               # 仍能扫到 AP
# 若需通网：再 join → net dhcp → ping（允许再走一遍）
gpio selftest 18          # 仍 PASS
gpio edgeselftest 18 4    # 仍否证 FAIL（勿变绿撒谎）
```

| # | 项 | ☐ |
|---|---|---|
| D-X1 | BT 后 WiFi scan 仍可用 | |
| D-X2 | BT 后 GPIO selftest 仍 PASS | |
| D-X3 | edgeselftest 否证仍成立 | |

### 3.6 真机账（2026-11-02 · FROM-DS）· **第2轮收官**

| 项 | 09:20 先 WiFi→bt | 09:28 / **09:45 复现** 冷启先 bt |
|---|---|---|
| GPIO / edgeselftest 否证 | ✅ / ✅ | ✅ / ✅（09:45 重跑） |
| WiFi bringup+scan | ✅ 挂网+ping | ✅ 4/4 + scan（bt 后仍活） |
| BT Ready | ❌ | ✅ `0B:3B:22:AC:88:20`（**可复现**） |
| 同电第 2 次 `bt init` | — | ❌ patch 非幂等（必红） |
| D-X1 | ❌ stage3 写挂→打挂 WiFi | 视失败点：先 bt 成功路径 **不红**；仅 stage4「已 applied」时 WiFi 仍可活 |

**顺序钉死**：冷启 **先 `bt init` 且本电只一次** → BT+WiFi 都绿；谁先落 patch 谁赢。D-X1 勿写成无条件。stage→`fmacfwbt` 永久切 → **另刀**。**第2轮回归结束**；串口可交 `audio-duo-1`。

---

## 4 · 两板对照速查

| 命令族 | Pi | Duo |
|---|---|---|
| 串口 | GPIO14/15 mini-UART | 板载 UART0 |
| WiFi 壳 | 多为 `wifi …` | `wififw` / `wifinet` / 通用 `net`/`ping` |
| BT | `bt` / `bt init`（`.hcd`） | `bt init`（SDIO `fmacfwbt` + UART4） |
| GPIO 安全脚 | 17/4/22/27；LED/BTN | **仅** 18/19/20（GP*） |
| 边沿自测 | 同脚 PASS | 同脚 **否证 FAIL** |

---

## 5 · 红线（回归时勿踩）

- 为过 edgeselftest **改 WiFi / 假 Ready / 软件伪造边沿**  
- Duo 误跑 `ck-pi-sd`；Pi/Duo 串口对错板  
- 扫波特率；改 Docs/25 数据面「顺便」修 BT  
- 白名单外 Duo GPIO（15/16 等）当正常脚猛拧  
- 未记录串口原文就改判「绿」

---

## 6 · 收口模板（可贴协作信箱）

```text
回归日：YYYY-MM-DD
Pi @ttyUSBx：P-G__/P-W__/P-B__ = 绿/红；原文：…
Duo @ttyUSBy：D-G__/D-W__/D-B__/D-X__ = 绿/红/否证成立；原文：…
结论：全绿 / 红项=… / 否证仍成立
```

---

## 7 · 已知基线（对照用，非本次必相等）

| 板 | 项 | 曾绿原文摘要 |
|---|---|---|
| Pi | BT | `addr=B8:27:EB:E8:99:11` name=Blocks |
| Duo | BT | `addr=0B:3B:22:AC:88:20` name=Blocks；fmacfwbt 333388 |
| Duo | ping | `ping_ok=1` → `192.168.31.1`；dhcp 例 `192.168.31.186` |
| Duo | G5 | snap：`ST=0 RAW=0`，INTEN 已置位 |
