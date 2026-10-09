# Runtime — QEMU 运行布局（X64）

```text
Runtime/
  Esp/X64/EFI/BOOT/BOOTX64.EFI   # 启动盘（disk0）
  RootFs/X64/Kernel.elf          # 系统盘（disk1，含 TOYOS.ID）
  RootFs/X64/TOYOS.ID
  Fw/OVMF_VARS.fd.clean          # NVRAM 种子
  sync.sh                        # 从 Boot/Kernel Build 拷贝产物
  run.sh                         # 起 QEMU
```

## 用法

```bash
cd ~/Blocks/Boot && ./build.sh
cd ~/Blocks/Kernel && ./build.sh x64
cd ~/Blocks/Runtime
./run.sh                 # 开 GTK 窗口（看屏 + 终端仍有串口）
./run.sh --headless      # 无窗口，只看串口
./run.sh --clean-nvram   # 重置 OVMF 变量
./run.sh --kill          # 杀残留 QEMU
```

看屏：用 **不要** `--headless` 的 `./run.sh`（本机有 `DISPLAY` 时会出窗）。窗内应见左上角 `ToyOS ready` / `Blocks>`（白字）；交互仍在终端串口。

依赖：`qemu-system-x86_64`、`/usr/share/OVMF/OVMF_CODE_4M.fd`。

串口在**启动 QEMU 的那个终端**里（`-serial stdio`），不要往 GTK 窗口里打字。

K16 预期：`Fs: TOYOS.ID ready (kernel)`（Root=`virtio-blk`+vvfat）；失败才退 Boot handoff。

K38：`ping` / `ping 10.0.2.2` → `ping: ok`（QEMU user 网关）；`ping: fail (arp|timeout)` 则查 virtio-net。

K39：`udplisten 40000` → `udpsend 10.0.2.15 40000 hello` → `udp: sent` + `udp: recv from 10.0.2.15:… hello`（本机 IP 回环）；也可再 `udprecv` 轮询。

K40：串口 `tcplisten 5000`，宿主机另开终端 `printf 'hello-k40\n' | nc -w2 127.0.0.1 15000` → 串口 `tcp: client connected` + `tcp echo: hello-k40`（`run.sh` hostfwd :15000→客 :5000）。`tcpconnect <ip> <port> <text>` 主动连+发。

K17 预期：`Gui: mouse ok` / `Gui: cursor on`；**点 GTK 窗**移动鼠标见光标；点顶栏串口 `Gui: bar click`。

K18 预期：`Scheduler: timer ok`；在 `Blocks>` 空闲时周期性 `Sched: tick …`；键入仍可用。

K19 预期：开机见 `User: load HELLO.ELF` → `Hello from HELLO.ELF` → `User: exit` → `Blocks>`；也可输入 `hello` 再跑。

K37：`THEME.CFG`（RootFs 根）。默认 **ink**；Settings：ink/slate/pine。  
字库：ASCII=Terminus **10×18**；汉字优先盘上 **`CJK32.BIN`**（默认 **18×18×4bpp**，~1.2MiB，对标现网）；缺文件回退内建 16×1bpp；再缺才 TTF。重生成：`python3 Kernel/Tools/gen-cjk32-bin.py`（`--dim 24|32` 可加大）。  
Shell `mode` 写分辨率；**mode 须冷启动**才被 Boot 消费（VM 内不热切 SetMode）。

默认分辨率 **1440×900**（亦支持 `1280x720` / `1600x900` / `1920x1080`）：`THEME.CFG` 的 `mode=`；`run.sh` 用 VGA edid 对齐（或 `TOY_QEMU_XRES/YRES` 覆盖）。改完须**退出 QEMU 再 `./run.sh`**。

布局：`LAYOUT.CFG`（或内建描述表）定窗 WxH / 栏高；设计稿 1280×720，真屏更小时 **只缩不放**。改窗大小优先改 CFG/描述表，勿在 `GuiWinLayoutAll` 写魔法数。

`run.sh`：VGA edid + `virtio-net-pci`（user 网）；鼠走 PS/2（不挂裸 xhci）。

可选：`./build.sh x64 SCREEN_LOG=1` 把 boot 日志镜像到屏（Y≥80）。
