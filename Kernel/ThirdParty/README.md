# ThirdParty

## lwIP

ToyOS 默认嵌入 [lwIP](https://savannah.nongnu.org/projects/lwip/) 2.2.x（`LWIP=1`）。源码目录：

```bash
git clone --depth 1 --branch STABLE-2_2_0_RELEASE \
  https://github.com/lwip-tcpip/lwip.git ThirdParty/lwip
```

构建：

```bash
./build.sh              # 默认 LWIP=1（含用户 socket / DNS）
./build.sh LWIP=0       # 关掉 lwIP，仅保留 builtin 教学栈
```

未检出源码时默认 `LWIP=1` 会编译失败；用 `LWIP=0` 可先编通内核。

PR-UI-ttf-1：`ThirdParty/stb/stb_truetype.h`（public domain v1.26），仅 `Services/Fonts/TrueTypeRaster.c` 在 FPU 岛内包含。

## FreeType

`font-freetype-hook`：上游树本地检出（gitignore，同 lwIP）；移植在 `freetype-port/`（进仓）。
版本**钉死**为 **2.13.2**（上游 tag `VER-2-13-2`），由脚本抓取并做两层校验，不手工 curl：

```bash
# 在 Kernel/ 下（bootstrap.sh 也会调用）
Scripts/fetch-freetype.sh          # 缺则下载；已有且校验通过则跳过（幂等）
Scripts/fetch-freetype.sh --check  # 只校验现有树，不联网
Scripts/fetch-freetype.sh --force  # 丢弃现有树重下
```

- tarball sha256：`427201f5d5151670d05c1f5b45bef5dda1f2e7dd971ef54f0feaaa7ffd2ab90c`
- 解包树内容指纹（730 文件）：
  `cd ThirdParty/freetype && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum | sha256sum`
  = `cb12af55da443b3af1e8c37aa701b13fec5424c9e279740da4c71b04d0ff8259`

缺树时 **x86_64 默认构建报错**（不再静默退 stb）：

```bash
./build.sh x86_64                 # 缺树 → error，提示跑抓取脚本
./build.sh x86_64 FREETYPE=0      # 显式降级：输出 note: 未使用 FreeType，字体走 stb
```

非 x86_64 内核本就只用 stb（`FreeTypeRaster.c` 仅 `__x86_64__` 启用），FreeType 不参与其构建。
有树时 x64 链 TT + smooth + psnames + autofit；`ftsystem` arena ≤256K（防 Boot BSS 装不下）。
许可：FreeType License / GPLv2 双许可，见树内 `LICENSE.TXT`。

移植文件：`HAL/X64/LwIp/`（`lwipopts.h`、`Netif.c`、`Ping.c`、`TcpEcho.c`、`UdpQueue.c`、`TcpClient.c`、`TcpSocket.c`）。

---

## 双栈策略（lwIP 默认路径 vs builtin 教学对照）

ToyOS 同时保留两套 IP/传输实现，但**运行时不同时处理同一帧**。

| 模式 | 何时 | RX 路径 | TX / Shell 命令 | 用途 |
|------|------|---------|-----------------|------|
| **lwIP（默认课路径）** | 默认 `./build.sh` 后 Shell `lwip on` | 仅 `ToyNetifInput` → lwIP | 同名命令改走 lwIP；用户 `socket` / `dns` / `NETDEMO`/`NETLIB` | 生产向协议栈；课堂默认 |
| **builtin（教学对照）** | `LWIP=0`，或已编入 lwIP 但**未** `lwip on` | `Net.c` → `HandleIpPacket` → ICMP / `Udp` / `Tcp` | 自研：`ping` / `udpsend` / `udplisten` / `tcplisten` / `tcpconnect` | 零第三方依赖、对照讲协议 |

### 规则（必须遵守）

1. **一帧一栈**：`lwip on` 置 `HalNetSetLwipReceive(1)` 后，入站帧不再进 builtin `HandleIpPacket`。
2. **`lwip on` 不可逆（当前会话）**：会 `TcpInit`/`UdpInit` 清空自研连接；builtin `TcpPoll` 停转；`NetSendIp` / `NetPing` 在 lwIP 活跃时失败。要回 builtin：重启 QEMU（无 `lwip off` 热切回）。
3. **命令名共用**：Shell 不区分两套 API；`LwIpActive()` 决定路由。看当前栈：`net` / `lwip status` / `tcpstatus`。
4. **主栈方向**：新功能（多连接、用户态 socket、DNS）落在 **lwIP**。自研 `Tcp.c` / `Udp.c` 为 **legacy 教学对照栈**，仅维持现有单连接联调，不再深化。

### 课堂默认路径（ping → dns → NETDEMO / NETLIB）

```bash
# 宿主机另开终端（NETLIB/NETDEMO 客户端目标）
nc -l -p 8888

cd ToyKernel && ./build.sh
cd ../ToyImage && ./run-split.sh
```

Guest Shell：

```text
ping 10.0.2.2
lwip on
dns 10.0.2.2
# 可选：dns example.com
exec NETLIB.ELF
# 或裸 syscall 对照：exec NETDEMO.ELF
```

成功：`netlib: ok`（或带 `recv=`）。`NETSRV.ELF` 监听 guest `:9000` echo；宿主可用 `hostfwd` 连入。

### 用户态 socket

| 系统调用 | 编号 | 参数 | 说明 |
|----------|------|------|------|
| `socket` | 8 | domain, type, protocol | 仅 `AF_INET` + `SOCK_STREAM`；首次调用自动 `LwIpInit` |
| `connect` | 9 | fd, ip(host u32), port | 主动连接 |
| `bind` | 10 | fd, ip(`0`=ANY), port | 绑定本地端口 |
| `listen` | 11 | fd, backlog | 进入监听 |
| `accept` | 12 | listen_fd | 返回新连接 fd（默认一直等到有连接） |
| `write`/`read`/`close` | 1/3/4 | 同文件 FD | socket fd 上即 send/recv/close |

库封装：`#include <Net.h>`（`libNet.a`）；演示 `NETLIB.ELF`；模板 `Bingo/App/Examples/Net/`。

### 快速对照（builtin 教学）

```bash
./build.sh LWIP=0
# toyos> ping 10.0.2.2
# toyos> tcplisten 9000
```

`run.sh` / `run-split.sh` 已配置 `hostfwd=tcp::9000-:9000`（冒烟脚本默认关 hostfwd）。
