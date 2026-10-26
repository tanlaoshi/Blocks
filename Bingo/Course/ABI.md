# 开课 ABI 冻结（课用面）

> **Bingo 正本**：本文件在`Bingo/` 下维护。  
> **身份**：Blocks 是全新系统，**不当 Linux 兼容层**（见 [README §0.1](../App/README.md)）。课上只教 **ISO C + Blocks API**。  
> **实现真源**：`Kernel/Include/Abi/SyscallABI.h`、`Kernel/User/include/*.h`、`lib*.a`。  
> **卡纸**：[`../Course/CheatSheet.md`](../Course/CheatSheet.md)。  
> Shell：`blocks>` · 卷：`BLOCKS:`。

---

## 一、冻结版本（课上承诺）

| 面 | 版本 | 头文件 / 库 | 说明 |
| -- | ---- | ----------- | ---- |
| CRT / ISO C | **1.6.0** | `stdio`/`stdlib`/`string`…；`Blocks/version.h`；`libBlocks.a` | `printf`/`malloc`/`fopen` 等 **ISO C**；文件/进程用 Blocks 名见下 |
| Syscall 号 | 以头文件为准 | `Abi/SyscallABI.h`、`Blocks/syscall.h` | **禁止**手写魔法数字；未知号 → **`-ENOSYS`（-38）** |
| UI | **3.0.0** | `Ui.h`；`libUi.a` | `UiCreateWindow` / `UiPoll`… |
| Graphics | **3.0.0** | `Graphics.h`；`libGraphics.a` | `GraphicsDamage*` / `GraphicsDraw*` |
| Net | **3.0.0** | `Net.h`；`libNet.a` | **只教** `NetConnect`/`NetBind`（主机序）+ `socket`/`send`/`recv` |
| 文件 / 目录 | Blocks | `Blocks/File.h`、`Blocks/Dir.h`；`VolumePath.h` | `FileOpen`/`FileRead`…；`DirOpen`/`DirRead`…；`FsUtil*` |
| 进程 / 线程 | Blocks | `Blocks/Process.h`、`Blocks/thread.h` | `ProcessFork`/`ProcessWait`…；`ThreadCreate`… |

**破坏性变更**：对应 `*_ABI_VERSION_MAJOR`（或 `CRT_VERSION_MAJOR`）必须 +1，并改本表 + 速查卡 + 示例。

---

## 二、承诺（课上可教、可考）

1. **ISO C**：stdio / 堆 / `fopen` 族；路径可用 `BLOCKS:NAME` 或默认卷相对名。  
2. **文件 / 目录（Blocks）**：`FileOpen` / `FileRead` / `FileWrite` / `FileClose`；`DirOpen` / `DirRead` / `DirClose`；或 `FsUtilListDir`。  
3. **进程 / 线程（Blocks）**：`ProcessFork` / `ProcessWait` / `ProcessExec`；`ThreadCreate` / `ThreadJoin`（教学子集，非完整 pthread）。  
4. **UI 3.x / Graphics 3.x**：见头文件。  
5. **Net 3.x**（内核 `LWIP=1` 且 Guest `lwip on`）：`socket` + **`NetConnect`/`NetBind`**（主机序，`NetIpv4`）。**不教** `sockaddr` / `htons` 课设路径。  
6. **商店目录包**：`Assets/Store/packages/<id>/` + `catalog.txt` → Guest `store combo`/`install`。

---

## 三、不承诺（勿当缺口催）

- Linux / 假 POSIX 兼容（`open`/`fork`/`opendir`/`pthread_*`/`sockaddr` 公开脸已拆除或降内部）  
- 完整信号、完整 `poll`/`select`  
- Guest 内编译 / 包管理器级依赖求解  
- 稳定 **ioctl** 号表、任意新 syscall 号  
- Arm64 / RiscV virt 上的 lwIP（常见 `LWIP=0`）  
- 旧 GUI syscall **18–21**；公开 **`toy_*`** 包装  

---

## 四、与仓库路径对照

| 课上说法 | 仓库位置 |
| -------- | -------- |
| 内核树 | `Kernel/` |
| 用户态 | `Kernel/User/`（CRT/库）；示例 `Bingo/App/{Template,Examples}` |
| Shell | Guest：`blocks>` |
| 还债剧本 | [`路线图1024 · selfhost`](../../Archive/路线图1024.md#selfhost) |

更细 API：[`API.md`](../App/API.md)。上手：[`Guide.md`](../App/Guide.md)。
