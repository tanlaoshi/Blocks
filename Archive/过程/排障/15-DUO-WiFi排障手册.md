# 15 · DUO WiFi 排障手册（可复现版）

> **这份手册的用法**：不要从头读。先看 §2 的五条命令跑一遍，拿到当前卡在哪一层，
> 再去 §3 查那一层的判据，最后去 §6 查对应的坑。
> 所有判据都是**串口上能 grep 到的原文**，不是"应该会打印"。

---

## 1 · 复现环境

| 项 | 值 |
|---|---|
| 板子 | Milk-V Duo S（CV1801B / SG2002），板载 AIC8800D80 WiFi |
| 启动路径 | u-boot 读 `boot.sd`（FIT，内核内嵌）在 `mmc 0:1` LABEL=boot；**不读 `BLOCKS/Kernel.elf`** |
| 串口 | UART0 `0x04140000`，16550，`115200 8N1`，USB-TTL 接调试口 |
| 固件 | `Source/DUO/sdk/.../aic8800_fw/`（5 段 + 101 条 patch） |
| 抓包机 | 任意 x86 + 支持 monitor 的网卡；本项目用 Intel 8265（`iwlwifi`） |
| 部署方式 | 串口差量写（`deploy.sh`）或读卡器（`ck-duo-sd.sh`） |

**串口上提示符长这样**：`Blocks> `（内核 shell）。发命令必须**以 `\r` 结尾**，
且**长命令要分片发送**（一次性写会丢字符，实测把 `BOOT:boot.sd` 丢成 `BO`）。

---

## 2 · 五条命令走完整链路

```bash
# ① 部署（改完代码后；约 3 分钟，串口差量）
cd /home/tank/Blocks
bash Kernel/HAL/RiscV/Board/milk-v-duo-s/scripts/deploy.sh \
     --no-build --elf Kernel/Build/duo-xxx/HAL/RiscV/Kernel.elf --port /dev/ttyUSB0

# ② 复位（部署脚本不复位，必须手动）
#    串口发：reboot

# ③ 固件下载（约 60 秒，101 条 patch）
#    串口发：wififw write confirm

# ④ 无线电配置 + 扫描
#    串口发：wififw bringup
#    串口发：wififw scan

# ⑤ 关联 + 握手 + 数据
#    串口发：wififw join Apple <密码>
#    串口发：wififw dhcp 6000
```

**每步的通过判据见 §3。** 任何一步没过，就停在那一步，别看后面。

---

## 3 · 九层判据表（照这个 grep）

| 层 | 通过判据（串口原文） | 失败时先看 |
|---|---|---|
| **L0 SDIO 枚举** | `WiFi: sdio verdict: awake=1` 且 `WiFi: chip id=` 非 unknown | `WiFi: sdio state: fn1 00=`（寄存器现场）、`WiFi: 4bit readback if=` |
| **L1 固件下载** | `WiFi: fw state=done (all stages ok)` | `WiFi: fw failed at stage N`；`WiFi: patch plan: walk ended at` |
| **L2 无线电配置** | `WiFi: bringup ok (`（完整形态 `bringup ok (4/4)`，是分几段拼出来的） | `WiFi: br cfg ... no CFM`（ME_CONFIG/CHAN/MM_START 三步的顺序是硬契约） |
| **L3 扫描** | `WiFi: scan summary: N APs from 3 rounds` 且出现真实 SSID | `WiFi: scan: round N ...`、`WiFi: scanu start cfm status=` |
| **L4 建 vif** | `WiFi: add if cfm status=0`（日志里是 `WiFi: add if cfm status=`） | `WiFi: add if send FAILED`、`plen=` 过短 |
| **L5 关联** | `WiFi: join cfm ok (status=0x00` + `WiFi: join ok (associated` | `SM_CONNECT_CFM status=0x09` = 已连着（不是失败）；`no SM_CONNECT_CFM` = 固件没在听，先做 L1–L3 |
| **L6 四次握手** | `WiFi: eapol rx len=… msg=M1` → `eapol tx m2` → `msg=M3` → `M3 mic ok` | 只来 M1 不回 M2：看 §6.1（RSN 组播套件）；M3 MIC 失败：PTK/密码错 |
| **L7 装密钥** | `WiFi: key add: ptk … cfm=0` 和 `key add: gtk … cfm=0` | `cfm=none`（2 秒内没等到 0x25）、`gtk: status=` 非 0（KDE 解包失败） |
| **L8 M4 收尾** | `WiFi: eapol tx m4 len=99` 且**之后 AP 不再重传 M3**（下一次 M3 的 `replay=` 不再涨） | M3 反复重传 ⇒ M4 没被接受：看 §6.2（顺序） |
| **L9 数据通路** | `WiFi: data frame after keys: len=… decr=2/3`、`WiFi: dhcp: OFFER rx`、`WiFi: arp uni: reply ok` | `net poll: window frames=0`（固件没给任何帧）或 `frames>0 但 decrypted=0`（密钥不生效） |

**判据串可以这样抓**（板端 shell 没有 grep，就在 PC 端记日志再 grep）：

```bash
# 用 python 打开串口、发命令、把输出存文件，然后 grep 判据
# 关键：115200 8N1，命令以 \r 结尾，长命令分片（每片 8 字节 + 4 ms 间隔）
```

---

## 4 · 决策树（按症状）

```
看不到任何 WiFi 日志
└─ 板子没起来 / 串口没接对 → 先确认 Blocks> 提示符

L0 不过（sdio verdict / chip id）
└─ 4bit readback if= 不对 → SDIO 总线宽度/时钟；见 WifiSdioState.c 的寄存器现场

L1 不过（fw failed at stage N）
└─ 固件文件缺失（FwTable）→ 检查 BLOCKS 上的固件；patch plan 报"越界/尾部不齐"是镜像不完整

L2 不过
└─ 检查发消息顺序：ME_CONFIG → ME_CHAN_CONFIG → MM_START，缺一个都没有 CFM

L3 扫不到目标 AP
└─ 先 wififw scan 两遍（多轮才稳）；确认信道/2.4G；SSID 隐藏要指定

L5 关联失败
├─ CFM status=0x09  → 已经连着，别重复 join；先 mmreset 或 reboot
├─ CFM 无响应       → 固件没跑起来（回 L1/L2），或上一次 join 没结束
└─ IND status≠0     → 802.11 状态码，见 WifiAssocReport.c 的人话映射

L6 握手卡在"没有 M1"
├─ 抓包看 AP 有没有发 M1（§5.2 的对照法）
├─ AP 不发 → 检查 RSN IE：**组播套件必须与 AP 的 beacon 一致**（§6.1）
└─ AP 发了我们没收到 → 收包/端口问题：检查 control port 是否在 IND 之后立刻打开

L6 卡在"没有 M3"
└─ M2 的 MIC 错了（KCK/PTK 派生）或密码错

L8 M3 反复重传
└─ M4 被拒：先看是不是"装完钥匙才发 M4"（§6.2）；再看 key_info 的 install 位

L9 数据不通
├─ frames=0            → 固件不上送任何帧（连广播都没有）⇒ 收发都被固件挡着
├─ frames>0 decrypted=0 → 帧来了但没解密 ⇒ 密钥没真正生效
└─ 我们的帧上空口但 AP 不回 ⇒ 加密用的密钥不对（本项目当前卡点，见 §7）
```

---

## 5 · 证据链（每条都有原始日志与复现方法）

### 5.1 结论 E1：RSN 组播套件必须与 AP 一致 —— 否则 AP 不开四次握手

**现象**：关联成功（`Assoc Response: PRIVACY, Successful`），之后 **30 秒内 0 条 EAPOL**。

**复现（抓包机在信道 8 上抓）**：

```bash
sudo rfkill unblock all
sudo ip link set wlp1s0 down
sudo iw dev wlp1s0 set monitor control
sudo ip link set wlp1s0 up
sudo iw dev wlp1s0 set channel 8
sudo tcpdump -i wlp1s0 -nn -e -s 200 -c 400 -w /dev/shm/x.pcap
```

```bash
tcpdump -r /dev/shm/x.pcap -nn -e | grep -ic eapol          # 0
tcpdump -r /dev/shm/x.pcap -nn -XX | grep -A30 -m1 "Assoc Request"   # 看我们的 RSN IE
```

**证据**：我们的 RSN IE 组播是 `00-0F-AC-04`(CCMP)，而 Linux（同板同 AP）是 `00-0F-AC-02`(TKIP)；
Linux 连上时 AP 立刻发 M1（4 帧 EAPOL），我们的内核 **0 帧**。

**修复**：`WifiAssocMessage.c` 的 RSN IE 组播改成 TKIP。改完握手立刻通。

### 5.2 结论 E2：M4 必须在装密钥之前发

**现象**：AP 的 M3 反复重传（`replay=` 从 1 涨到 3），数据全不通。

**原因**：装完密钥后，固件把后续发出的帧（含 EAPOL）加密 → AP 收到密文 M4 → 判为无效 → 重传 M3。

**修复**：`WifiHandshakeInstall.c` 的 `AIC_HANDSHAKE_M4_BEFORE_KEYS = 1`。
**修好后的判据**：M3 的 `replay` 停在 1，不再重传。

### 5.3 结论 E3：我们的 M4 与 Linux 的 M4 前 49 字节一致

两边都是：`01 03 00 5f | 02 | 03 0a | 00 00 | replay=<M3 的值> | nonce=全 0`。
⇒ 报文格式、`key_info`、`key_length`、`replay`、`nonce` 全部排除。

**复现**：板端 `wififw join` 会打 `WiFi: m4 bytes [...]`；Linux 侧抓包后用 `-XX` 看同一位置。

### 5.4 结论 E4：广播帧的 `staid` 只能用 `ap_idx`

| staid | 空口结果 |
|---|---|
| `ap_idx`（0） | 帧发得出去，但 **KeyID=0**（用 PTK 加密） |
| `0xFF` / `32` / `8` / `10` / `16` / `64` / `128` | **固件根本不发**（抓包 262 帧、我们的 0 帧） |

**复现**：改 `BLOCKS:probe.cfg` 的 `mcast=<n>`（§5.6），抓包看有没有我们的帧。

### 5.5 结论 E5：GTK 装法不是变量

6 种组合全部失败（都以真机 DHCP 结果为准）：keyid=0/1/2、cipher=TKIP/CCMP、
pairwise=0/1、长度 16/32、装完再发一次 `ME_SET_CONTROL_PORT`。

**且 44 字节 `MM_KEY_ADD` 报文已逐字段核对**（`WiFi: key add payload len=44 [...]`）：
`key_idx=2 sta=0xFF len=32 cipher=TKIP inst=0 spp=0 pairwise=0`，固件回 `cfm=0`。

### 5.6 结论 E6：文件探针把"一轮 5 分钟"压到 20 秒

板子在 `wififw dhcp` 启动时读 `BLOCKS:probe.cfg`（一行一个键值）：

```
mcast=<n>     广播帧 txdesc staid 覆盖
ac=<n>        txdesc ac 覆盖
tid=<n>       txdesc tid 覆盖
start=1       先发一条 EAPOL-Start 探针
arpuni=1      单播 ARP 问 AP 自己（192.168.31.1）
m4install=0   M4 不置 install 位（join 时读）
```

写文件（2 秒）：

```bash
printf "mcast=32\n" > /tmp/probe.cfg
python3 Tools/ksend.py --port /dev/ttyUSB0 --file /tmp/probe.cfg \
        --path BLOCKS:probe.cfg --no-reboot --no-backup
```

---

## 6 · 已知的坑（每一条都踩过）

### 6.1 别用"漏发消息"的框架解释握手失败
真正的根因是**字段值不对**（RSN 组播套件）。设备 bring-up 里，"少一条消息"远比"某个字段和厂商不一样"少见。

### 6.2 打印会改变时序
115200 下一行 60 字符 ≈ 5 ms。AP 的 M1 在关联完成瞬间就到，**控制端口必须在打印之前打开**
（`WifiAssoc.c` 里那句"抢开控制端口"就是这个原因）。

### 6.3 串口命令必须分片发
一次性写 `wififw join Apple xxx` 会丢字符；按 8 字节 + 4 ms 间隔发，并在发完后**校验目标路径**再执行。

### 6.4 部署前必须确认板子在提示符
否则 `deploy.sh` 会报 `board is not at a shell prompt`，白等三分钟。先探一次 `Blocks>`。

### 6.5 扫描缓存与 mmreset
`MM_ADD_IF` 建的 vif 会被扫描/关联复用；`wififw mmreset` 后**必须清掉 vif 缓存**，
否则下一次 `join` 会拿到失效 vif（现象：`join cfm status=0x04`）。

### 6.6 SDIO 读很慢
实测 **512 B / 次、单次往返约 25 ms** ⇒ 2.1 MB 读一遍约 10 分钟。
所以 `deploy.sh` 才有 `.crc` 块表缓存；**用读卡器写完必须删掉同名 `.crc`**，否则下次差量会用过期基准。

### 6.7 抓包机会过热掉线（本项目最大的环境坑）
Intel 8265 在 monitor 模式下跑 tcpdump 超过 ~30 秒会满负载、过热、掉网。
**对策**：`-s 160 -c 300`（限帧数、限长度）、抓完立刻 `pkill -INT tcpdump`、只抓关键 10 秒。

### 6.8 `wifi` 与 `wififw` 是两套命令
Duo 上只有 `wififw`；`wifi` 是别的板/壳的入口。把探针挂到 `wifi` 上等于没挂。

---

## 7 · 当前卡点（交接点）

**唯一没解决的一件事**：广播/组播帧，固件用 **PTK** 加密（空口 802.11 头 `KeyID=0`），
而正确应是 **GTK**（`KeyID=2`，这是抓 Linux 的 DHCP 广播看到的）。

厂商驱动靠 **BCMC 队列** 发广播：`rwnx_tx.c:511-521` 取
`sta_idx = NX_REMOTE_STA_MAX(32) + master_vif_idx(vif)`；
而我们填 32 时固件**直接不发帧** ⇒ 说明固件里没有这个 BCMC 站。

**下一步该查的**（按可能性）：
1. `MM_ADD_IF` 之后固件是否创建 BCMC sta（是否需要某个 flag/附加消息）；
2. GTK 是否真的写进了固件的 key table（没有"读回密钥"的消息，只能靠行为推断）；
3. 收方向是否根本没解密（`decrypted=0` 一直没变过）。

---

## 8 · 源码地图

```
Sdhci/
  WifiSdioState.c         L0 寄存器现场与判决（awake/devirq/blocks）
  WifiFw.c / WifiProbe.c  L1 固件下载、patch、版本查询
  WifiBringup.c           L2 ME_CONFIG/CHAN/MM_START
  WifiScan*.c             L3 扫描请求/结果/多轮合并
  WifiAssoc*.c            L4/L5 ADD_IF、SM_CONNECT、IND 解析、RSN IE
  WifiEapol*.c            L6/L9 收帧、聚合解析、EAPOL 分发
  WifiHandshake*.c        L6/L8 M2/M4 组帧、PTK 派生、GTK 解包、装密钥顺序
  WifiKeyAdd.c            L7 MM_KEY_ADD 镜像 + payload dump
  WifiNetFrame.c          L9 以太网帧、校验和、收包 sink
  WifiDhcp.c / WifiArp.c  L9 DHCP 客户端、ARP（含单播探针）
```

**三条不变量（改代码前先读）**：
1. RSN IE 的组播套件必须跟 AP 的 beacon 一致；
2. M4 必须在装密钥之前发；
3. `gSector` 是"调用者手里的那个扇区"，FAT 链遍历不许借用它（这是另一个已修的损坏 bug，
   见 `Kernel/Library/Fat/InputOutput.c` 的注释）。
