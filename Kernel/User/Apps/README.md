# User/Apps 政策（user-apps-1）

权威表：课设从哪编、是否进 Class/Store。课外毛坯只在 [`Start/App/`](../../../../Start/App/)。  
开窗 **只信** PKG `window=yes`（禁止 Class 空框窗 Demo：GUIDEMO / WINDEMO / BLITDEMO）。

## 总表

| 源 | 政策 | 产物 / 落点 | 说明 |
| --- | --- | --- | --- |
| `Hello.c` | **Kernel 编** | `hello.elf` → Class `HELLO`/`MYAPP`；Store `hello` | 无窗入门 |
| `Cat.c` | **Kernel 编** | `catfile.elf` → Store `cat` | 无窗 |
| `WriteFile.c` | **Kernel 编** | `writefile.elf` → **仅 Build**（不预装） | 自拷 Class/`exec` |
| `Snake.c` | **Kernel 编** | Store `snake`（`window=yes`） | **不**预装 Apps/；`store install` |
| `TaskMgr.c` | **Kernel 编** | Store `taskmgr`；预装 `Apps/taskmgr/`（`window=yes`） | |
| `Chat/` | **Kernel 编** | Store `chat`（`window=yes`） | 不预装桌面 |
| `Demos/*.c` | **Kernel 编** | 见下「Demo 分列」 | 勿迁 Start |
| `Assembly/*.S`（表内） | **Kernel 编** | Class / Build | 裸 syscall 课 |
| `Assembly/CatFile.S` `HelloInt80.S` `WriteFile.S` | **删** | — | 未挂 Makefile；CRT 版已替代 |
| Start `Examples/Hello|Gui|Net|…` | **Start Example** | 本地 `build/` + SDK | 课外模板；**不是** Class 源 |
| Start `Examples/UiText|UiDesigner` | **Start Example** | 树内 `BlocksUser.mk` | 待 `uitxt2h` 回仓 |

**不迁 Start**：Snake / Chat / TaskMgr / 全套 Demos·Assembly（跟核 `Makefile`·`build.sh` 绑死）。  
**不删（保留 Kernel）**：上表「Kernel 编」行。

## Demo / Assembly 分列（Kernel 编）

### Class 预装（`build.sh` → `RootFs/X64/Class/`）

| ELF | 源 |
| --- | --- |
| HELLO / MYAPP | `Hello.c` |
| COUNT FORK WAITNH | `Assembly/Count|Fork|WaitNoHang.S` |
| LIBTOY.SO DYNDEMO | `Assembly/LibToy|DynamicDemo.S` |
| SYSHELLO SYSFORK | `Assembly/SyscallHello|SyscallFork.S` |
| EXECDEMO PIPEDEMO | `Demos/ExecDemo|PipeDemo.c` |
| THREADSMOKE PTHREADSMOKE THREADDEMO | `Demos/Thread*.c` |
| ENOSYS | `Demos/EnosysDemo.c` |
| SOCKDEMO NETLIB | `Demos/SocketDemo|NetLibDemo.c` |
| DIRDEMO | `Demos/DirDemo.c` |

### 仅 Build（编进 `Build/User/`，不预装 Class）

| ELF | 源 |
| --- | --- |
| BRKDEMO MMAPDEMO KILLDEMO SIGDEMO | 对应 `Demos/*` |
| LIBCDEMO SLEEPDEMO CWDDEMO | 对应 `Demos/*` |
| WRITE | `WriteFile.c` |
| NETDEMO NETSRV | `Assembly/NetDemo|NetServer.S` |

### Store catalog（与 `Runtime/Store/catalog.txt` 一致）

| id | window | 预装 Guest Apps/ |
| --- | --- | --- |
| hello | 否 | `Apps/hello/` |
| cat | 否 | 否 |
| taskmgr | **yes** | `Apps/taskmgr/` |
| snake | **yes** | 否（须 `store install`） |
| chat | **yes** | 否 |
| sun8 / demopack | font/asset | — |

## 维护

- 改预装名单：同步改本表 + `Kernel/build.sh` +（若 Store）`catalog.txt` / `PKG.TXT`。  
- 新开窗课设：Store 包目录 + `window=yes`；**禁止**再往 Class 丢空框窗 ELF。
