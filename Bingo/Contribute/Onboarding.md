# Blocks 开发者接手指南（开课后自学）

> **写给**：要自己改内核 / 应用的人。  
> **读完能**：知道文档去哪找、接口以谁为准、怎么编/验、常见坑。  
> **命名**：仓库与产品 **Blocks**；Guest **`blocks>`**；卷 **`BLOCKS:`**。

---

## 0. 三句话

1. **活 ★ / 日档**认当日 `Archive/路线图MMDD.md` 文首（协作暗号见路线图「〇」）；月进展写 [`../项目/进展/`](../../Archive/项目/进展/)。  
2. **用户可见 ABI**只认 [`开课ABI冻结.md`](../App/ABI.md) + `Kernel/User/include/` 宏；syscall 号只认 [`SyscallABI.h`](../../Kernel/Include/Abi/SyscallABI.h)。  
3. **写新代码**守 [`编码规范.md`](../../Archive/规范/编码规范.md)；驱动/USB 等先查标准（见 `.cursor/rules/standards-first`）。

---

## 1. 文档地图

| 你要做的事 | 先读 | 再读 |
| ---------- | ---- | ---- |
| 写用户应用 / 作业 | [`应用开发指南.md`](../App/Guide.md) | [`学生速查卡.md`](../Course/CheatSheet.md)、[`API速查.md`](../App/API.md) |
| 查函数一眼 | [`API速查.md`](../App/API.md) | `Kernel/User/include/` |
| 印大纲 / 对版本 | [`开课ABI冻结.md`](../App/ABI.md) | 头文件 `*_ABI_VERSION_*` / `CRT_VERSION_*` |
| 仓库树 / 启动 | [`仓库与启动/Blocks目录结构.md`](../System/Tree.md) | [`仓库与启动/Blocks开机流程.md`](../System/Boot.md) |
| 写驱动 | [`驱动开发指南.md`](../Driver/Guide.md) | 技术手册、规格书 |
| 协作暗号 | 路线图「〇、协作暗号」 | JX / TG / TS / TB / TBU / TBN / GD |

**已完/** 历史规格里的 ABI 号可能旧；**以头文件 + 开课ABI冻结为准**。

---

## 2. 开课冻结面（摘要）

| 组件 | 版本 | 核对 |
| ---- | ---- | ---- |
| CRT | **1.5.0** | `Blocks/version.h` → `CRT_VERSION_STRING` |
| UI | **3.0.0** | `Ui.h` → `UI_ABI_VERSION_*`；API `Ui*`；`libUi.a` |
| Graphics | **3.0.0** | `Graphics.h`；`Graphics*`；`libGraphics.a` |
| Net | **3.0.0** | `Net.h`；API **`Net*`**；`libNet.a` |
| FsUtil | **2.0.0** | `VolumePath.h` → `FS_UTIL_*`；`libVolumePath.a` |
| SDK 包 | 见 `Tools/Sdk/VERSION` | 与上表独立 |

```bash
cd "$BLOCKS_ROOT/Kernel"   # 或 source ~/Blocks/env.sh 后用 $KERNEL
rg -n 'VERSION_STRING|_ABI_VERSION_STRING' User/include/Blocks/version.h \
  User/include/Ui.h User/include/Graphics.h User/include/Net.h User/include/VolumePath.h
```

### 2.1 Syscall（易错）

- 权威：`Include/Abi/SyscallABI.h`；用户侧 `#include <Blocks/syscall.h>`。  
- GUI 用 **1000+** / `Ui*` / `Graphics*`；**禁止**旧号 18–21。  
- 未知号 → **`-ENOSYS`（-38）**。

### 2.2 进程等待（Blocks）

| | |
|--|--|
| `ProcessWait(&status)` | `*status = (exit & 0xff) << 8` |
| `WEXITSTATUS(s)` | `(s >> 8) & 0xff` |
| **没有** | 按 pid 等待（非 Linux `waitpid`） |

### 2.3 文件红字

- `fopen("w")` **不截断** → 先 `remove(path)` 再 `fopen`。  
- `FileOpen` 的 flags 内核多半忽略。  
- **`FileWrite` 返回 0 视为错误**（与 FileRead EOF=0 不同）。

### 2.4 GUI / HID

`UiPoll`：`UI_EVENT_NONE` / `CLOSE` / `CLICK` / `UI_BUTTON_EVENT(id)` / `UI_KEY_EVENT(hid)` 等（见 `Ui.h`）。  
常用 HID：Enter `0x28`、Esc `0x29`、Backspace `0x2A`、空格 `0x2C`、方向 `0x4F`–`0x52`。

### 2.5 网络

- 内核 x86 默认常 `LWIP=1`；Guest 先 **`lwip on`**。  
- **只教** `NetConnect`/`NetBind`（主机序，`NetIpv4`）。不教 `sockaddr`。  
- Arm64/RiscV virt：常见 `LWIP=0`，无网络课端口。

---

## 3. 仓库与日常命令

**单仓**：根下 `Boot/` · `Kernel/` · `Runtime/` · `Image/`（或 Runtime 镜像布局）· `Archive/` · `env.sh`。

```bash
source ~/Blocks/env.sh          # 短名：build / run / smoke / sync …

build                           # x64 Kernel（默认）
build arm64                     # QEMU virt（默认 BOARD=virt）
build riscv BOARD=milk-v-duo-s
boot                            # Boot → BOOTX64.EFI
run                             # x64 QEMU（对标旧 run-split）
smoke

# 仍可用树内脚本（env 未 source 时）
cd "$BLOCKS_ROOT/Kernel" && ./build.sh
```

**U 盘 / NUC**：暗号 **TBU** / **TBN**（`sync usb` / `sync nuc`）。Wi‑Fi 凭证勿打串口 PSK。

**验收**：`Kernel/Tools/Scripts/test-all.sh`（须镜像旁路齐）；`Kernel/Scripts/test/` 下 expect 冒烟。

**SDK**：`Kernel/Tools/build-sdk.sh` → `Build/BlocksSDK`；动手入口 **`Bingo/App/Template`** · **`Bingo/App/Examples`**。

---

## 4. 写代码时的硬规则（摘要）

1. 新 **`.c` ≤300 行**；将满先拆。**文档不限行数。**  
2. 新符号 PascalCase + 层前缀。  
3. 改 `Common/` / `User/` / `Include/` 时兼顾 **x86 / arm64 / riscv**（至少抽验）。  
4. JX 收口写清要不要手测；能量化的用 `test-*.sh`。  
5. **TG** = 只 commit；**TS** = push；**GD** = 只挪路线图、不 commit。

可读性细则：[`代码可读性规范`](../../Archive/规范/细则备查/可读性.md)。键鼠优先：见工作区「鼠标优先」铁律。

---

## 5. 真机 / 外设（课机摘要）

| 项 | 状态 |
| -- | ---- |
| NUC 有线 / iwl Wi‑Fi | 课路径可用（凭证 `FW/WIFI.CFG`；勿串口打 PSK） |
| USB HID 键鼠 | 优先保流畅；勿饿死 xHCI |
| Arm 板（Pi / Duo 等） | 见日档 / Board 包；公司机可先 QEMU virt |

细节以路线图归档与驱动柱为准。

---

## 6. 已知缺口（别当 bug 狂修）

- Linux 兼容 / 假 POSIX 脸 / 完整 pthread / `.so` 全重定位  
- Graphics **位图**、完整 TTF  
- Guest 内编译、公网商店  
- Arm/RiscV 上的 lwIP  

承诺面见 [`开课ABI冻结.md`](../App/ABI.md)「不承诺」。

---

## 7. 建议自学顺序

1. `Bingo/App/Examples/Hello`（或复制 `Template`）→ 进镜像 `exec`（巩固 CRT）。  
2. 读 `SyscallABI.h` + `Core/Syscall/Syscall.c`（分发与 ENOSYS）。  
3. 改一刀用户库（`User/Library/Ui`）看 ABI MAJOR 纪律。  
4. 驱动：对照 [`Driver/Guide.md`](../Driver/Guide.md)；Board 外逻辑保持架构通用。  
5. 动调度 / FS 前读模块化归档 + [`Course/Homework/`](../Course/Homework/)。

---

## 8. 改接口时的文档同步清单

1. `User/include/` 宏 / `SyscallABI.h` / `Errno.h`  
2. [`开课ABI冻结.md`](../App/ABI.md)  
3. [`学生速查卡.md`](../Course/CheatSheet.md)  
4. [`API速查.md`](../App/API.md)  
5. [`应用开发指南.md`](../App/Guide.md)（示例代码）  
6. 技术手册 GUI/网络表若仍引用旧名  
7. 日档 / 路线图（若是排期柱）

**已完/** 旧 ABI **不要回头改**。

---

## 9. 常用路径速查

| 路径 | 用途 |
| ---- | ---- |
| `Kernel/Include/Abi/SyscallABI.h` | syscall 号权威 |
| `Kernel/Core/Syscall/Syscall.c` | 分发 + ENOSYS |
| `Kernel/User/crt/` | CRT |
| `Kernel/User/Library/{Ui,Graphics,Net,VolumePath}/` | 用户库 |
| `Kernel/User/Apps/` | 课设 / 镜像自带 Demo（政策见该目录 README） |
| `Bingo/App/Template` · `Examples/` | **第一颗 ELF** 入口 |
| `Kernel/Tools/Scripts/test-all.sh` | 统一验收 |
| `env.sh`（仓根） | `build` / `run` / `sync` 短名 |
| `Bingo/System/{Tree,Boot}.md` | 目录结构 · 开机流程 |
