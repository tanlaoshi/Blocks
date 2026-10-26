# 开课 ABI 冻结（课用面）

> **身份**：Blocks 是全新系统，**不当 Linux 兼容层**（见 [`身份.md`](身份.md)）。课上只教 **ISO C + Blocks API**。  
> **实现真源**：`Kernel/Include/Abi/SyscallABI.h`、`Kernel/User/include/*.h`、`lib*.a`。  
> Shell：`blocks>` · 卷：`BLOCKS:`。  
> 本文件在 `Start/App/`。

---

## 一、冻结版本（课上承诺）

| 面 | 版本 | 头文件 / 库 | 说明 |
| -- | ---- | ----------- | ---- |
| CRT / ISO C | **1.5.0** | `stdio`/`stdlib`/`string`…；`Blocks/version.h`；`libBlocks.a` | 与 `CRT_VERSION_*` 对齐；文件/进程用 Blocks 名见下 |
| Syscall 号 | 以头文件为准 | `Abi/SyscallABI.h`、`Blocks/syscall.h` | **禁止**手写魔法数字；未知号 → **`-ENOSYS`（-38）** |
| UI | **3.0.0** | `Ui.h`；`libUi.a` | `UiCreateWindow` / `UiPoll`… |
| Graphics | **3.0.0** | `Graphics.h`；`libGraphics.a` | `GraphicsDamage*` / `GraphicsDraw*` |
| Net | **3.0.0** | `Net.h`；`libNet.a` | **只教** `NetConnect`/`NetBind`（主机序）+ `socket`/`send`/`recv` |
| FsUtil | **2.0.0** | `VolumePath.h`；`libFsUtil.a` | `FsUtilBlocksPath` / `FsUtilListDir`… |
| 文件 / 目录 | Blocks | `Blocks/File.h`、`Blocks/Dir.h` | `FileOpen`/`FileRead`…；`DirOpen`/`DirRead`… |
| 进程 / 线程 | Blocks | `Blocks/Process.h`、`Blocks/thread.h` | `ProcessFork`/`ProcessWait`…；`ThreadCreate`… |

**破坏性变更**：对应 `*_ABI_VERSION_MAJOR`（或 `CRT_VERSION_MAJOR`）必须 +1，并改本表 + 速查卡 + 示例。

---

## 二、承诺（课上可教、可考）

1. **ISO C**：stdio / 堆 / `fopen` 族；路径可用 `BLOCKS:NAME` 或默认卷相对名。  
2. **文件 / 目录（Blocks）**：`FileOpen` / `FileRead` / `FileWrite` / `FileClose`；`DirOpen` / `DirRead` / `DirClose`；或 `FsUtilListDir`。  
3. **进程 / 线程（Blocks）**：`ProcessFork` / `ProcessWait` / `ProcessExec`；`ThreadCreate` / `ThreadJoin`（教学子集，非完整 pthread）。  
4. **UI 3.x / Graphics 3.x**：见头文件。开窗 App 必须 **`Apps/<id>/` + `PKG.TXT` 且 `window=yes`**（否则 CoopDrain，关窗/鼠钉死；见 [`红线.md`](红线.md)）。  
5. **Net 3.x**（内核 `LWIP=1` 且 Guest `lwip on`）：`socket` + **`NetConnect`/`NetBind`**（主机序，`NetIpv4`）。**不教** `sockaddr` / `htons` 课设路径。  
6. **商店 / 包目录**：`Store/packages/<id>/` 或 `Apps/<id>/`（`PKG.TXT` + ELF）→ Guest `store combo`/`install` 或 `exec Apps/<id>/…`。

---

## 三、不承诺（勿当缺口催）

- Linux / 假 POSIX 兼容（`open`/`fork`/`opendir`/`pthread_*`/`sockaddr` 公开脸已拆除或降内部）  
- 完整信号、完整 `poll`/`select`  
- Guest 内编译 / 包管理器级依赖求解  
- 稳定 **ioctl** 号表、任意新 syscall 号  
- Arm64 / RiscV virt 上的 lwIP（常见 `LWIP=0`）  
- 旧 GUI syscall **18–21**；公开 **`toy_*`** 包装  
- 扁平 `Apps/FOO.ELF`（无包目录）当开窗 App  

---

## 四、与仓库路径对照

| 课上说法 | 仓库位置 |
| -------- | -------- |
| 内核树 | `Kernel/` |
| 用户态 / 毛坯 | 本树 `Start/App/`；工具链 `Kernel/Build/BlocksSDK` |
| Shell | Guest：`blocks>` |

更细 API：[`API.md`](API.md)。上手：[`Guide.md`](Guide.md)。
