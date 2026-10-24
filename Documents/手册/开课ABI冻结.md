# 开课 ABI 冻结（课用面）

> **课用真源**：本页。同学只认这里的版本与「承诺 / 不承诺」。  
> **实现真源**：`Kernel/Include/Abi/SyscallABI.h`、`Kernel/User/include/*.h`、`lib*.a`（以仓库为准）。  
> **卡纸**：[`学生速查卡.md`](学生速查卡.md)（本表的打印摘要）。  
> **命名**：产品与仓库为 **Blocks**；Guest Shell 提示符 **`blocks>`**；卷前缀 **`BLOCKS:`**。旧笔记里的 ToyOS / `toyos>` / `TOYOS:` 视为同义，写代码请用新名。

---

## 一、冻结版本（课上承诺）

| 面 | 版本 | 头文件 / 库 | 说明 |
| -- | ---- | ----------- | ---- |
| CRT | **1.5.0** | `Blocks/version.h` → `CRT_VERSION_*`；`libBlocks.a` | ISO C：`printf`/`malloc`/`fopen`…；Blocks：`File*`/`Process*`；`sleep`/`ThreadYield` |
| Syscall 号 | 以头文件为准 | `Abi/SyscallABI.h`、`Blocks/syscall.h` | **禁止**手写魔法数字；宏名本位（`SYS_FILE_OPEN`…）；未知号 → **`-ENOSYS`（-38）** |
| UI | **3.0.0** | `Ui.h` → `UI_ABI_VERSION_*`；`libUi.a` | 高层窗 / Label / Button / `UiPoll` |
| Graphics | **3.0.0** | `Graphics.h` → `GRAPHICS_ABI_VERSION_*`；`libGraphics.a` | `GraphicsDamageText` / `GraphicsDamageRect` / `GraphicsDraw*` |
| Net | **3.0.0** | `<Net.h>` / `ToyNet.h` → `NET_ABI_VERSION_*`；`libToyNet.a` | `socket` + `NetConnect`/`NetBind`（主机序）；不教 `sockaddr` |
| FsUtil / VolumePath | **2.0.0** | `VolumePath.h` → `FS_UTIL_ABI_VERSION_*`；`libVolumePath.a` | `FsUtilJoin` / `FsUtilListDir`…；底层 `Dir*` |
| 线程子集 | 课可用 | `Blocks/thread.h`、`<pthread.h>`（自旋 mutex） | `Thread*`；mutex 可用 `<pthread.h>`；**非**完整 POSIX 线程 |

**破坏性变更**：对应 `*_ABI_VERSION_MAJOR`（或 `CRT_VERSION_MAJOR`）必须 +1，并改本表 + 速查卡 + 示例。

---

## 二、承诺（课上可教、可考）

1. **CRT 1.5.x**：ISO C stdio/堆；Blocks `File*` / `Process*`（`ProcessWait` + `WEXITSTATUS`）；路径可用 `BLOCKS:NAME` 或默认卷相对名。  
2. **UI 3.x**：`UiCreateWindow` → `UiSetLabel` / `UiAddButton` → `UiPoll`；事件常量见 `Ui.h`。  
3. **Graphics 3.x**：在已有窗口上 damage / 画点线矩形（需链 `libGraphics.a`）。  
4. **Net 3.x**（内核 `LWIP=1` 且 Guest `lwip on`）：`socket` + `NetConnect`/`NetBind`（主机序，`NetIpv4`）。  
5. **目录**：`DirOpen` / `DirRead` / `DirClose` / `FileStat`；或 `FsUtilListDir`。  
6. **商店目录包**：`Assets/Store/packages/<id>/` + `catalog.txt` → Guest `store combo`/`install`（见应用开发指南 §打包）。

---

## 三、不承诺（勿当缺口催）

- 完整 POSIX（完整 `pthread`、信号、完整 `poll`/`select` 语义等）  
- Guest 内编译 / 包管理器级依赖求解  
- 稳定 **ioctl** 号表、任意新 syscall 号  
- Arm64 / RiscV virt 上的 lwIP（常见 `LWIP=0`，无网络课端口）  
- 旧 GUI syscall **18–21**（已废；课只用 `Ui*` / `Graphics*`）

---

## 四、Net / 目录（只教 Blocks 名）

| 面 | 课用 API |
| -- | -------- |
| Net | `socket` + `NetConnect` / `NetBind`（**主机序**，`NetIpv4`）；`<Net.h>`。不教 `sockaddr`/`htons` |
| 目录 | `DirOpen` / `DirRead` / `FileStat`；便利 `FsUtilListDir`（`<Blocks/Dir.h>` / `<VolumePath.h>`） |

---

## 五、与仓库路径对照（防迷路）

| 课上说法 | 仓库位置 |
| -------- | -------- |
| 内核树 | `Kernel/`（构建入口 `./build.sh`；环境 `source ./env.sh`） |
| 用户态 / SDK | `Kernel/User/`；离树示例 `Kernel/Tools/Sdk/Examples/` |
| 镜像 / QEMU | `Image/`（`prepare-rootfs`、`run-split.sh`） |
| Shell | Guest：`blocks>` |
| 目录结构 / 开机 | [`Blocks目录结构.md`](Blocks目录结构.md)、[`Blocks开机流程.md`](Blocks开机流程.md) |

更细 API 表：[`API速查.md`](API速查.md)。上手步骤：[`应用开发指南.md`](应用开发指南.md)、[`开发者接手指南.md`](开发者接手指南.md)。
