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

K17 预期：`Gui: mouse ok` / `Gui: cursor on`；**点 GTK 窗**移动鼠标见光标；点顶栏串口 `Gui: bar click`。

K18 预期：`Scheduler: timer ok`；在 `Blocks>` 空闲时周期性 `Sched: tick …`；键入仍可用。

`run.sh`：`-device qemu-xhci` + `virtio-net-pci`（user 网）。

可选：`./build.sh x64 SCREEN_LOG=1` 把 boot 日志镜像到屏（Y≥80）。
