# 12 · 设备驱动 bring-up 方法论（DUO WiFi 实战总结，给 PI 线照做）

> **写给**：在 PI 上做 WiFi 的 Cursor。
> **来源**：DUO S + AIC8800D80 全程，从"完全不动"到"WPA2 握手 + 装密钥 + 数据通路"。
> **一句话**：**先装眼睛，再改代码；每次只改一个变量；失败也要留下可复查的记录。**

---

## 0 · 先看结论：这次真正起作用的是哪几步

| 阶段 | 做法 | 效果 |
|---|---|---|
| 摸清协议 | 读厂商 u-boot / Linux 驱动源码，**结构体用编译期 sizeof/offsetof 核对** | 消息通路一次就通（扫描/关联） |
| **卡住时** | **换一台机器做空口抓包（monitor）** | **一次对照就定位到真因** |
| 定位后 | 改**一个常量**（RSN 组播套件 CCMP→TKIP） | 握手立刻通 |
| 收尾 | 装密钥 + 回 M4 + 监视解密帧 | 安全链路建立 |
| 全程 | 快部署闭环（内核自写 boot.sd，4.9 秒） | 一轮试验从 40 分钟降到 1 分钟 |

**最贵的一课**：在装眼睛之前，我们连试了 **11 个假设，全部错**。装上抓包后，**一次对照就找到**。

---

## 1 · 铁律：卡住时先装"眼睛"，不要加猜测

### 1.1 症状 vs 证据

```
症状：关联成功后收不到 EAPOL M1
症状：只有 beacon 上送、数据帧不上送
症状：改了 N 处都没用
```

**症状不是证据。** 只要出现"我们以为 A，但现象不支持"的循环，**立刻停下来装观测手段**。

### 1.2 本次的"眼睛"

**一台 x86 机器 + 支持 monitor 的网卡**（Intel 8265，`iwlwifi`）：

```bash
sudo rfkill unblock all
sudo ip link set wlp1s0 down
sudo iw dev wlp1s0 set monitor control
sudo ip link set wlp1s0 up
sudo iw dev wlp1s0 set channel 8          # 锁到目标 AP 的信道
sudo tcpdump -i wlp1s0 -nn -e -s 0 -w /tmp/cap.pcap
```

**关键操作：做对照组**
1. 让**厂商 Linux** 连一次（已知能成功）→ 抓包，确认"AP 会发什么"；
2. 让**我们的内核**连一次 → 抓包；
3. **两份抓包逐字节对比**。

### 1.3 对照怎么比（这次的做法）

```bash
# 1) 有没有 EAPOL（ethertype 0x888e）
tcpdump -r cap.pcap -nn -e | grep -ic eapol

# 2) 谁的帧
tcpdump -r cap.pcap -nn -e | grep -c "1c:36:bb:04:fd:80"

# 3) 把关联请求整帧打出来，逐字段比
tcpdump -r cap.pcap -nn -XX | grep -A30 -m1 "Assoc Request"
```

**这次的结果**：
- Linux 连接：AP 立刻发 M1（4 帧 EAPOL）；
- 我们的内核：整段只有 beacon，**0 条 EAPOL**；
- 两条 assoc 请求逐字节对比，**只有 RSN IE 的组播套件不同**：
  ```
  Linux:  30 14 0100 000fac02 ...   ← 组播 TKIP
  我们:   30 14 0100 000fac04 ...   ← 组播 CCMP（错）
  ```
- 改这一个常量 → 握手立刻通。

> **教训**：设备 bring-up 里，**"字段值不对"远比"少发一条消息"常见**。
> 不要一上来就怀疑"漏了什么"，先怀疑"发的某个字段和厂商不一样"。

---

## 2 · 用厂商源码的正确姿势

### 2.1 抄寄存器序列，但**结构体布局必须编译期验证**

错误做法：照着头文件手数字段偏移 → 差 4 字节就全乱（我们踩过：`e2a` 的参数在 +16 不是 +12）。

正确做法：**在宿主 gcc 里 include 厂商头文件，打印 sizeof/offsetof**，再把值写成常量 + 运行时自检。

```c
/* 我们的做法：每个消息一个 LayoutCheck，发送前核对，不符就拒发 */
int AicKeyAddLayoutCheck(void) {
    if (sizeof(AIC_MM_KEY_ADD_REQ_BODY) != 44) return -1;
    if (offsetof(AIC_MM_KEY_ADD_REQ_BODY, StaIndex) != 1) return -1;
    ...
}
```

**这条让我们在字段级错误上几乎没有返工。**

### 2.2 厂商的"默认值"要原样抄

`ps_on`、`uapsd_queues`、`listen_itv`、`dont_wait_bcmc` 这些，**照厂商默认值填**；
改它们只能是"有证据的实验"，改完要能说清"为什么"。

### 2.3 注意**两套编号**

本次踩到：`MAC_CIPHER_TKIP=1 / CCMP=2`（驱动内部）与 `RSN 套件编号 TKIP=2 / CCMP=4`（空口）**是两套**。
代码里必须**分开命名**（`AIC_KEY_CIPHER_TKIP` vs `AIC_HANDSHAKE_GROUP_CIPHER_TKIP`），否则一定写错。

---

## 3 · 日志：判据行 + 现场取证

### 3.1 每条关键路径都要有一行"可 grep 的判据"

```
WiFi: join cfm ok (status=0x00, auth procedure started)
WiFi: control port open (vif=0 ap=0)
WiFi: eapol rx: … msg=M1
WiFi: eapol rx: M3 mic ok (AP accepted our M2; PTK matches)
WiFi: key add: ptk sta=0 cipher=ccmp(2) rc=0 cfm=0
WiFi: handshake ok (keys installed: ptk+gtk)
WiFi: data after keys: frames=0 data=0 decrypted=0
```

**格式统一**：`模块: 事件 (关键字段)`。这样历史日志可以直接用 grep 判读，不需要人肉比对。

### 3.2 失败时打"现场"，不是打"失败"

```c
WiFi: sdio state: fn1 00=00 01=10 02=00 03=7f 04=00 ...
WiFi: sdio verdict: awake=1 devirq=0 blocks=0
```

**verdict 那一行是给人看的结论**，前面的寄存器是给下一个排查者看的证据。

### 3.3 打印也要算成本

115200 串口下**一行 ~60 字符 ≈ 5 ms**。在"AP 立刻发 M1"这种时序敏感的地方，
**打印会改变行为**（我们为此专门把控制端口的发送提前到打印之前）。

---

## 4 · 每次只改一个变量

### 4.1 一轮的固定形状

```
假设（一句话，能被证伪）
 → 最小改动（一个常量 / 一条消息 / 一个顺序）
 → 部署
 → 一条判据命令
 → 结果写进"失败记录"，无论成败
```

### 4.2 失败记录是资产，不是流水账

我们的 `Docs/11-DUO-WiFi失败记录.md` 里，每条都是：

```
假设 → 做法 → 真机结果（日志原文）→ 结论（证伪/成立）
```

**好处**：
- 不会重复试同一个假设；
- 新接手的人（或未来的自己）能一眼看到"这条路已经堵死"；
- **写出"为什么错"比写出"改对了"更有价值**。

---

## 5 · 部署闭环（这是效率的地基）

**PI 上现在改内核还要插拔卡。** DUO 已经做到：

```bash
deploy.sh --port /dev/ttyUSB0
# build → 生成 boot.sd(FIT) → 串口差量写卡 → 整文件 CRC 回读校验 → 提示可复位
```

**关键设计**（都可移植到 PI）：

| 机制 | 作用 | 注意 |
|---|---|---|
| 4 KB 块差量 + 块 CRC 表缓存（`.crc`） | 只传变化的块；READY 前不读整个文件 | 缓存必须能被删除退回慢路径 |
| 写前备份 + 写后整文件 CRC 回读 | 失败能回滚，**绝不"报成功但内容错"** | 外部写入（读卡器）必须让缓存失效 |
| `--no-backup` | 可重新生成的文件不必备份 | 失败时要能说清"卡可能半新半旧" |
| 复位由操作者显式触发 | 避免"传完自动重启"打断排查 | RTC 温复位在 DUO 已验证 |

**PI 的差异**：PI 的启动文件是**普通内核文件**（不需要 FIT），所以 deploy 更简单；
串口要接 GPIO14/15 的 USB-TTL。

---

## 6 · PI 上照做时的具体建议

1. **先把"眼睛"准备好**：PI 那颗 BCM43430 建议用**另一台机器 monitor 抓包**（或者先用厂商 Linux 抓自己的时序作为参照）。
2. **固件下载**：BCM43430 也是"先下载固件再跑"，和 AIC 一样；用厂商 `brcmfmac` 的日志/源码对照。
3. **结构体**：`brcmf` 的协议是 TLVs，同样**用编译期/单位置解析**，别手算偏移。
4. **失败记录**：一开始就建 `Docs/13-PI-WiFi失败记录.md`，格式照 `Docs/11`。
5. **部署闭环**：把 `ksend` 的板级串口换成 PI 的 UART（协议层不用改），`fs selftest` / `fs hole` 已经跨平台通用。

---

## 7 · 一句话总结

> **设备驱动不是"写代码"，是"建立观测 → 提出可证伪的假设 → 用最小改动验证"的循环。**
> **观测的成本，永远低于猜错的成本。**
