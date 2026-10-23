# Blocks API 速查（课用）

> 与 [`开课ABI冻结.md`](ABI.md) 对齐。实现以头文件为准。  
> Shell：`blocks>` · 卷：`BLOCKS:` · CRT **1.5.0** · UI/Graphics/Net **3.0.0** · FsUtil **2.0.0**

---

## 一、构建与头文件

| 用途 | 头文件 | 库（Sdk / User 链） |
| ---- | ------ | ------------------- |
| CRT / 版本 | `<stdio.h>` … `<Blocks/version.h>` | `libcrt.a` |
| Syscall | `<Blocks/syscall.h>` | （CRT 内联） |
| UI | `<Ui.h>` | `libUi.a` |
| Graphics | `<Graphics.h>` | `libGraphics.a` |
| Net | `<ToyNet.h>` | `libToyNet.a` |
| 卷路径 | `<VolumePath.h>` | `libVolumePath.a` |
| 目录 | `<dirent.h>` | CRT / FsUtil |
| 线程 | `<Blocks/thread.h>` / `<pthread.h>` | CRT |

离树：`source ./env.sh` 后 `cd Kernel/Tools/Sdk/Examples/<Hello|Gui|Net> && make`。  
树内示例：`Kernel/User/Apps/*Demo.c`（随 `Kernel/./build.sh` 进镜像）。

---

## 二、CRT（常用）

| API | 说明 |
| --- | ---- |
| `printf` / `puts` | 焦点在用户窗时输出走**串口** |
| `malloc` / `free` | 用户堆 |
| `open` / `read` / `write` / `close` / `lseek` | 文件；路径 `BLOCKS:NAME` 或相对名 |
| `fopen` / `fread` / `fwrite` / `fclose` | FILE* |
| `fork` / `wait` / `WEXITSTATUS` | 进程 |
| `getpid` / `getppid` | |
| `sched_yield` | 让出 |
| `sleep` / `usleep` | |
| `exit` | |

未知 syscall → **-38**（`-ENOSYS`）。

---

## 三、UI 3.0（`Ui.h`）

| API | 说明 |
| --- | ---- |
| `UiAbiNegotiate` | 可选；查 ABI |
| `UiCreateWindow(title, w, h)` | 成功返回 window id ≥0 |
| `UiSetLabel(wid, text)` | |
| `UiAddButton(wid, index, text)` | |
| `UiPoll(wid)` | `UI_EVENT_NONE` / `UI_EVENT_CLOSE` / `UI_BUTTON_EVENT(i)` … |
| `UiDestroyWindow` 等 | 见头文件 |

示例：`Tools/Sdk/Examples/Gui/main.c`；树内 `User/Apps/GuiDemo.c`。

---

## 四、Graphics 3.0（`Graphics.h`）

| API | 说明 |
| --- | ---- |
| `GraphicsInitialize` | |
| `GraphicsDamageText(wid, text)` | 客户区一行字 |
| `GraphicsDamageRect(wid, &desc)` | 像素矩形 blit |
| `GraphicsDrawPixel` / `DrawLine` / `FillRect` / `DrawRect` | 简单绘制 |

底层窗 / damage：见 `User/Apps/WinDemo.c`（若存在）与技术手册 GUI 节。

---

## 五、Net 3.0（`ToyNet.h`）

| 轨 | API | 字节序 |
| -- | --- | ------ |
| 1 | `socket` + `connect`/`bind` + `sockaddr_in` | **网络序**（`htons`/`htonl`） |
| 2 | `NetConnect(fd, ip, port)` / `NetBind` | **主机序**；`NetIpv4(a,b,c,d)` |

| API | 说明 |
| --- | ---- |
| `NetResolve` / `NetGetAddrIn` | 域名 |
| `NetConnectIn` / `NetBindIn` | `NET_SOCK_ADDR_IN` |

Guest 先 `lwip on`。模板：`Tools/Sdk/Examples/Net/main.c`。

---

## 六、目录与卷（FsUtil 2.0）

| API | 头文件 | 说明 |
| --- | ------ | ---- |
| `OpenDirectory` / `ReadDirectory` / `CloseDirectory` / `FileStat` | `dirent.h` | F4 第 2 轨；`FS_DIR` / `FS_DIR_ENT` |
| `opendir` / `readdir` | 宏 / 兼容 | |
| `FsUtilJoin` / `FsUtilVolumePath` / `FsUtilBlocksPath` | `VolumePath.h` | 拼 `BLOCKS:…` |
| `FsUtilListDir` / `FsUtilStat` / `FsUtilIsDir` | 同上 | |
| `FS_VOL_BLOCKS` / `FS_VOL_ESP` / `FS_VOL_RES` | 卷名常量 | |

属性：`FS_ATTR_DIR` 等见 `dirent.h`。

---

## 七、线程子集

| API | 说明 |
| --- | ---- |
| `toy_thread_create` / `join` / `exit` / `toy_gettid` | `Blocks/thread.h` |
| `pthread_create` / `pthread_mutex_*`（自旋） | `<pthread.h>`；**非**完整 pthread |

自测：`blocks> exec THREADDEMO.ELF`（若镜像已含）。

---

## 八、商店 / 安装（Shell）

| 命令 | 说明 |
| ---- | ---- |
| `store combo <id>` / `store install <id>` | 目录包 → `Apps/<id>/` |
| `store uncombo` / `store remove` | 卸装 |
| `exec <path>` | 跑 ELF，如 `exec Apps/hello/HELLO.ELF` 或根上 `HELLO.ELF` |

包布局：`Assets/Store/packages/<id>/` + `catalog.txt`（见应用开发指南）。

---

## 九、勿用

- GUI 旧 syscall **18–21**  
- 手写 syscall 号（用 `Blocks/syscall.h` / `SYS_*`）  
- `ToyNetConnect` / `ToyUi*` 旧名（现为 `Net*` / `Ui*`）  
- 把 `TOYOS:` 写进新代码（用 `BLOCKS:` / `FsUtilBlocksPath`）
