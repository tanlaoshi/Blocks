# Blocks API 速查（课用）

> 与 [`ABI.md`](ABI.md) 对齐。实现以头文件为准。  
> Shell：`blocks>` · 卷：`BLOCKS:` · **只教 ISO C + Blocks 名**（无 POSIX 双轨）。

---

## 一、构建与头文件

| 用途 | 头文件 | 库 |
| ---- | ------ | --- |
| ISO C | `<stdio.h>` `<stdlib.h>` `<string.h>` … | `libBlocks.a` |
| Syscall | `<Blocks/syscall.h>` | （内联） |
| 文件 / 目录 | `<Blocks/File.h>` `<Blocks/Dir.h>` | CRT |
| 进程 | `<Blocks/Process.h>` | CRT |
| 线程 | `<Blocks/thread.h>` | CRT |
| UI / Graphics | `<Ui.h>` `<Graphics.h>` | `libUi` / `libGraphics` |
| Net | `<Net.h>` | `libNet.a` |
| 卷路径 | `<VolumePath.h>` | `libVolumePath.a` |

---

## 二、ISO C（常用）

| API | 说明 |
| --- | ---- |
| `printf` / `puts` | 焦点在用户窗时输出走**串口** |
| `malloc` / `free` | 用户堆 |
| `fopen` / `fread` / `fwrite` / `fclose` | FILE* |
| `exit` | |

未知 syscall → **-38**（`-ENOSYS`）。

---

## 三、文件 / 目录（Blocks）

| API | 说明 |
| --- | ---- |
| `FileOpen` / `FileRead` / `FileWrite` / `FileClose` / `FileSeek` | 路径 `BLOCKS:NAME` 或相对名 |
| `DirOpen` / `DirRead` / `DirClose` / `FileStat` | 目录；`FS_DIR` / `FS_DIR_ENT` |
| `MemoryMap` / `MemoryUnmap` | `<sys/mman.h>`；匿名或文件私有映射 |
| `FsUtilJoin` / `FsUtilListDir` / `FsUtilBlocksPath` | `VolumePath.h` |

---

## 四、进程 / 线程（Blocks）

| API | 说明 |
| --- | ---- |
| `ProcessFork` / `ProcessWait` / `ProcessExec` / `ProcessGetPid` | 进程 |
| `ThreadCreate` / `ThreadJoin` / `ThreadExit` / `ThreadYield` | 教学线程子集 |

---

## 五、UI / Graphics / Net

| 面 | 主 API |
| -- | ------ |
| UI | `UiCreateWindow` / `UiSetLabel` / `UiAddButton` / `UiPoll` |
| Graphics | `GraphicsDamageText` / `GraphicsDraw*` |
| Net | `socket` + **`NetConnect`/`NetBind`**（主机序）+ `send`/`recv`；`NetIpv4` |

Guest 先 `lwip on`。模板：[`../Examples/Net/main.c`](../Examples/Net/main.c)。

---

## 六、商店 / 安装（Shell）

| 命令 | 说明 |
| ---- | ---- |
| `store combo <id>` / `store install <id>` | 目录包 |
| `exec <path>` | 跑 ELF |

---

## 七、勿用

- GUI 旧 syscall **18–21**  
- 手写 syscall 号  
- 公开 **`toy_*`**、`sockaddr`/`htons` 课设路径、假 POSIX 双轨文档  
- 新代码写 `TOYOS:`（用 `BLOCKS:`）
