# HostChat

宿主机中继 + GUI/CLI 客户端，给 Guest `CHAT.ELF` 半双工聊天。对标 HotSend：Meson + GTK4。

## 架构

```
Guest CHAT.ELF ──┐
                 ├──► 本机 TCP 中继 :9090
本机 hostchat-gui ─┘
chatd ─────────────┘
```

推荐：

```bash
source ~/Blocks/env.sh
serve chat              # 前台中继；另开终端可开 GUI
# 或
Kernel/Tools/HostChat/Build/meson/chat-serve [port]
```

- **服务器**：`serve chat` / `chat-serve`；或 GUI 内「启动服务器」（若本机已在 serve 则 GUI 只当客户端）。
- **本机 GUI**：连 `127.0.0.1:端口`，首行 `HOSTCHAT-GUI`；收 `+PEER`/`MSG`，发 `SEND id text`。
- **Guest / chatd**：普通文本行 peer，连宿主局域网 IP（或 `127.0.0.1`）。

## 布局（GUI）

微信式：左会话列表 / 右上消息 / 右下发送。顶栏：端口 / 启动服务器 / 停止。

## 构建

```bash
cd Kernel/Tools/HostChat
meson setup Build/meson
meson compile -C Build/meson
```

产物：

- `Build/meson/hostchat-gui` — 中继 + 本机 GUI 客户端
- `Build/meson/chatd` — CLI peer（连已启动的服务器）

```bash
Build/meson/hostchat-gui
# 另开终端测 peer：
Build/meson/chatd 127.0.0.1 9090
```

桌面 / 应用菜单：

```bash
./Tools/install-desktop.sh
```

装到「应用」与桌面快捷方式（`com.blocks.hostchat`），双击即可开 GUI。

## 双方如何发现？

中继在宿主上；Guest **主动连**宿主 IP：

| 角色 | 动作 |
| --- | --- |
| 宿主 | `hostchat-gui` →「启动服务器」（默认 `:9090`） |
| Guest `CHAT.ELF` | 连接 `chat.peer`（IP:端口） |

- **QEMU SLIRP**：Guest 默认 `10.0.2.2:9090`。
- **真机 / NUC**：`dbset chat.peer 192.168.x.x:9090`（宿主有线 IP，例如 `192.168.31.124:9090`）。

## Guest（商店 Chat）

1. 宿主已「启动服务器」  
2. Guest：`lwip on`（若尚未有网）  
3. 桌面点 Chat，或 `exec Apps/chat/CHAT.ELF`；首次可点 Reconnect
