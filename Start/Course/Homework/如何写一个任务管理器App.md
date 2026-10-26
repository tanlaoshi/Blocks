# 如何写一个任务管理器 App（端到端范例）

> **目标读者**：会 C、第一次在 **Blocks** 上做带窗口的应用。  
> **成品**：`TASKMGR.ELF`（源码 `Kernel/User/Apps/TaskMgr.c`）——窗内显示与 Shell `ps` / `list tasks` **同源**的任务表；可 Refresh、翻页（按钮或上下键）、Kill 用户任务；文案跟 `BLOCKS.DB` 的 `lang=`。  
> **配套**：[`应用开发指南.md`](../应用开发指南.md)；ABI [`开课ABI冻结.md`](../开课ABI冻结.md)；[`API速查.md`](../API速查.md)。

本文按「想清楚 → 开接口 → 写 App → 编进盘 → 跑通 → 进菜单」顺序写，路径均相对 **`Blocks/Kernel`**（单仓）。

---

## 0. 你要做的事（一句话）

| 层 | 做什么 | 本范例落点 |
| -- | ------ | ---------- |
| **产品** | 用户可见的窗口程序 | 标题「任务管理器」/ `Task Manager`；按钮 刷新 / 下一项 / 结束 |
| **数据** | 用户态读不到内核 `gTasks[]`，必须 syscall | `SYS_TASK_SNAP`（1200）+ `<Blocks/task.h>` |
| **UI** | 链 `libUi.a`（再链图形库） | `UiCreateWindow` / `UiSetLabel` / `UiAddButton` / `UiPoll` |
| **构建** | Makefile 出 ELF，`build.sh` 同步 RootFs | `Build/User/taskmgr.elf` → `TASKMGR.ELF` + `Apps/taskmgr/TASKMGR.ELF` |
| **发现** | 开始菜单 Apps / `exec` / 商店 catalog | catalog：`taskmgr\|…\|Task Manager` |

---

## 1. 先想清楚：能展示什么？

Shell 里敲 `ps`（别名；正统名 `list tasks`）——实现：`Services/Shell/Commands/SystemProcess.c` → `CommandPs`：

| 字段 | 含义 |
| ---- | ---- |
| `pid=` | **槽位 + 1**（与 `kill <pid>` 一致，不是裸 `TASK.Id`） |
| `Name` | 任务名（`shell` / `gui` / `worker` / ELF 基名等） |
| `user` / `kern` | 用户态还是内核任务 |
| `zombie` / `blocked` | 状态（省略则视为可运行） |
| `ticks` / `cpu` / `home` / `prio` | 调度统计 |

**适合放进 App 的额外信息**（不必新开 syscall）：

| 来源 | 字段 | 本范例 |
| ---- | ---- | ------ |
| 快照 syscall | `FreePages` / `CpuTicks` / `WorkerLoops` / `StealCount` | 结构里有；窗内列表优先 pid/名/类型/状态 |
| CRT | `getpid()` / `kill` | Kill 时对照 `SelfPid` |
| 已有 | `kill(pid, SIGKILL)` | 选中用户任务后结束（拒杀内核与自己） |

**刻意不做的**：改优先级 UI、画 CPU 历史曲线、读 `/proc`（Blocks 无此 VFS）。

---

## 2. 开课接口：`SYS_TASK_SNAP`

用户态**禁止** `#include` 内核 `Scheduler.h`。要把 `ps` 数据交给 App，只能：

1. 在 **`Include/Abi/SyscallABI.h`** 占号（本范例 **`SYS_TASK_SNAP = 1200`**）。  
2. **稳定布局**：`Include/Abi/TaskSnap.h`（内核与用户共用 `TASK_SNAP` / `TASK_ENTRY`）；用户包装头 `User/include/Blocks/task.h`（`blocks_task_snap()`）。  
3. 内核实现：`SysTaskSnap`（`Core/Syscall/Process.c`），在 `SyscallDispatch`（`Core/Syscall/Syscall.c`）里分发。  
4. **同步文档**：开课 ABI / API 速查 / 本指南（改字段须升 `TASK_SNAP_VER`）。

### 2.1 布局约定（教学钉死）

```text
TASK_SNAP
  Magic='TPS1'  Version=1  Count  FreePages
  CpuTicks  WorkerLoops  StealCount  SelfPid
  Tasks[16] → { Pid, Parent, Flags, Priority, OnCpu, HomeCpu, Ticks, Name[16] }

Flags: TASK_F_USER | ZOMBIE | BLOCKED | CURRENT
Pid / Parent / SelfPid：与 Shell ps、kill 相同（槽位+1）
```

宽度由 `BLOCKS_STATIC_ASSERT` 钉死（见 `TaskSnap.h`）。

### 2.2 调用形态

```c
#include <Blocks/task.h>

TASK_SNAP Snap;
if (blocks_task_snap(&Snap) != 0) { /* 失败 */ }
/* Snap.Magic == TASK_SNAP_MAGIC && Version == TASK_SNAP_VER */
```

`blocks_task_snap` 是头文件内联：`blocks_syscall(SYS_TASK_SNAP, buf, sizeof(*buf), 0)`。

---

## 3. 写 App：`User/Apps/TaskMgr.c`

### 3.1 依赖库怎么选

| 需求 | 库 | 头 |
| ---- | -- | -- |
| 窗口 / 按钮 / 标签 / 事件 | **libUi** | `<Ui.h>` |
| （Ui 依赖）图形 | libGraphics（或现网等价 `.a`） | 一般不必直接 include |
| printf / snprintf | CRT | `<stdio.h>` |
| yield / kill | CRT | `<sched.h>` `<signal.h>` |
| 任务快照 | （无 .a，仅头+syscall） | `<Blocks/task.h>` |

链接：`taskmgr.o` → `libUi.a` → 图形库 → CRT（见 `Makefile` 中 `USER_TASKMGR_*`）。

### 3.2 UI 结构（跟现网范例）

- 用 **`UiSetLabel`** 画多行表（制表符分列 + `\\n`）；**不是**靠未绘文字的 List 控件。  
- 语言：读根目录 **`BLOCKS.DB`** 的 `lang=zh` / `lang=en`（缺省汉语）。  
- **下一项** / 键盘上下：移动 `gIndex`；**刷新** 重拉快照；**结束** 杀当前用户任务。  
- 事件：`UiPoll`；无事可 `sched_yield()`。关窗：`UI_EVENT_CLOSE`。

### 3.3 安全与课堂演示点

- Kill 前检查：`TASK_F_USER`、不是 `SelfPid`。  
- USER 窗焦点下 `printf` **只走串口**。  

### 3.4 源码地图

| 函数 | 作用 |
| ---- | ---- |
| `Refresh` / `Paint` | syscall + 标签表 |
| `Move` / `KillCurrent` | 翻页 / kill |
| `main` | 开窗、按钮、事件环 |

---

## 4. 挂进构建系统

仓库内示范**已挂好**；自己加同类 App 时对照：

1. **`Makefile`**：`USER_TASKMGR_OBJ` / `USER_TASKMGR_ELF`，`all` 依赖，链接 Ui+Gfx+CRT。  
2. **`build.sh`（x86 同步盘）**：拷到 `Runtime/RootFs/X64/TASKMGR.ELF`，并 `Apps/taskmgr/TASKMGR.ELF`（包目录）。  
3. **商店 catalog**（`Services/Store/Catalog/Load.c` Builtin）：  
   `taskmgr|app|1|TASKMGR.ELF|-|x86_64|Task Manager`

**FAT 8.3**：盘上基名大写 `TASKMGR.ELF`。

---

## 5. 编译、部署、验收

```bash
source ~/Blocks/env.sh
build                    # x64；含 taskmgr.elf 并 sync 进 Runtime
run                      # 或：cd Runtime && ./run.sh
# 产物核对：
#   Kernel/Build/User/taskmgr.elf
#   Runtime/RootFs/X64/TASKMGR.ELF
#   Runtime/RootFs/X64/Apps/taskmgr/TASKMGR.ELF
```

Guest：

```text
blocks> exec TASKMGR.ELF
```

或：开始菜单 → **Apps** → **Task Manager** / **任务管理器**。

**验收清单**

1. 窗能开；列表含编号/名称/类型/状态（中或英随 `lang=`）。  
2. **下一项** / 上下键能换选中行（`>`）。  
3. 另开 `exec HELLO.ELF`，**刷新** 后行数变化。  
4. 选中**其它用户**任务 → **结束** → 刷新后消失。  
5. 对照：`blocks> ps`（或 `list tasks`）与窗内 pid/名一致。

真机：`sync nuc`（**TBN**）或 `sync usb`（**TBU**），见 [`同步.md`](../../Archive/同步.md)。

---

## 6. 和「课外 Pkg / SDK」的关系

| 路径 | 何时用 |
| ---- | ------ |
| `User/Apps/TaskMgr.c` + 内核 `Makefile` | **仓库内示范 / 开课镜像自带** |
| `User/Pkg/` | 学生课外独立工程 |
| `Tools/build-sdk.sh` → `Build/BlocksSDK.tar.gz` | 离树 SDK；头里应有 `Blocks/task.h` / `TaskSnap.h` |

---

## 7. 你自己做下一个 App 时的检查单

1. **要不要新 syscall？** 只读已有 ABI → 不必；碰调度表 → 新号 + 用户头 + 文档同步。  
2. **UI**：按钮/标签 → `Ui.h`；像素游戏 → Graphics（参考 `Snake.c`）。  
3. **stdout**：有 USER 窗时默认串口；给人看的字写进 `UiSetLabel`。  
4. **进菜单**：`Apps/<pkg>/…ELF` 或 catalog。  
5. **三架构**：syscall 共用；用户 ELF 分盘（课主路径 `RootFs/X64`）。  
6. **命名**：源文件 PascalCase；盘上 8.3；见 [`命名规范.md`](../../规范/命名规范.md)。  
7. **单文件宜短**；过长再拆。

---

## 8. 相关文件速查

| 角色 | 路径 |
| ---- | ---- |
| App | `User/Apps/TaskMgr.c` |
| 用户包装头 | `User/include/Blocks/task.h` |
| 布局（核/用户共用） | `Include/Abi/TaskSnap.h` |
| 号段 | `Include/Abi/SyscallABI.h` |
| 内核实现 | `Core/Syscall/Process.c` → `SysTaskSnap` |
| 分发 | `Core/Syscall/Syscall.c` |
| Shell 对照 | `Services/Shell/Commands/SystemProcess.c` → `CommandPs` |
| 构建 | `Makefile` / `build.sh` |

---

## 9. 常见问题

**Q：为什么不直接让 App `#include Scheduler.h`？**  
A：用户态没有内核符号与地址空间。教学上强制走 syscall。

**Q：`getpid()` 和列表里的 pid 不一样？**  
A：`ps` / `kill` / 快照用 **槽位+1**；以 `SelfPid` 与 Kill 为准。

**Q：提示符是 `toyos>` 还是 `blocks>`？**  
A：现网 Guest 提示符为 **`blocks>`**（旧文写 toyos 的已废）。
