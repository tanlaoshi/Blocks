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

看 K1 右上角色块：用 **不要** `--headless` 的 `./run.sh`（本机有 `DISPLAY` 时会出窗）。

依赖：`qemu-system-x86_64`、`/usr/share/OVMF/OVMF_CODE_4M.fd`。

串口在**启动 QEMU 的那个终端**里（`-serial stdio`），不要往 GTK 黑屏窗口里打字。

K8 预期：`ToyOS ready` → `Blocks>`；在终端输入会回显，回车后再打一行。

旧 K1 日志形如：`handoff:` → `FB WxH` → `video self-test` → `[Mod] …`。
