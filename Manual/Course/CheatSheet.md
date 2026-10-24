# Blocks 学生速查卡（A4 · 课程承诺面）

> **Manual 正本**：本文件在仓库根 `Manual/` 下维护；`Documents/手册/` 同期副本仅过渡期保留。


> 印刷 ABI 号见 [`开课ABI冻结.md`](ABI.md)；函数表见 [`API速查.md`](../App/API.md)。  
> **自学总入口**：[`开发者接手指南.md`](../Contribute/Onboarding.md)。  
> Shell：`blocks>` · 卷：`BLOCKS:`。**ISO C + Blocks API**；不承诺 Linux 兼容 / 完整线程 / 动态链接 / TTF。

---

## 进程（`<Blocks/Process.h>`）

| 签名 | 成功 | 失败 / 注意 |
| ---- | ---- | ----------- |
| `ProcessFork()` | 子 `0`；父=子 Id | `-1` |
| `ProcessWait(&status)` | 子 Id；`*status=(exit&0xff)<<8` | `-1`；`WEXITSTATUS` |
| `ProcessExec(path, argv, envp)` | 不返回 | `-1` |
| `exit(status)` | — | ISO C |
| `ProcessGetPid()` / `ProcessGetPpid()` | Id | |
| `ThreadYield()` | `0` | `<Blocks/thread.h>` |
| `sleep` / `usleep` | 0 | 按调度节拍 |

```c
long p = ProcessFork();
if (p == 0) { exit(7); }
int st; ProcessWait(&st); /* WEXITSTATUS(st)==7 */
```

## 线程（`<Blocks/thread.h>`）

| 签名 | 注意 |
| ---- | ---- |
| `ThreadCreate` / `ThreadJoin` / `ThreadExit` | 教学子集 |
| `ThreadMutexLock` / `Unlock` | **自旋**教学版 |

---

## 文件

| 签名 | 成功 | 失败 / 注意 |
| ---- | ---- | ----------- |
| `FileOpen(path)` | `fd≥0` | `-1`；可用 `BLOCKS:NAME` |
| `FileRead` / `FileWrite` | 字节数；EOF=`0` | `-1`；**Write=0 视为错误** |
| `FileClose` / `FileSeek` | `0` / 新偏 | `-1` |
| `FILE *fopen(path,mode)` | 非 NULL | ISO C；**`"w"` 不截断**→先 `remove` |
| `printf` / `snprintf` | 字符数 | 焦点在用户窗时多走**串口** |
| `FsUtilBlocksPath` 等 | 0 | `<VolumePath.h>`；FsUtil **2.0.0** |

```c
remove("A.TXT");
FILE *f = fopen("A.TXT", "w");
fwrite("hi", 1, 2, f); fclose(f);
```

---

## 内存

| 签名 | 成功 | 失败 / 注意 |
| ---- | ---- | ----------- |
| `malloc` / `calloc` / `realloc` / `free` | 指针 / — | NULL |
| `void *brk(void *addr)` | 新 break | `(void*)-1` |
| `mmap` / `munmap` | 地址 / `0` | `-1`；课用子集 |

---

## 任务快照（系统信息）

| 签名 | 注意 |
| ---- | ---- |
| `int blocks_task_snap(TASK_SNAP *out)` | `<Blocks/task.h>`；成功 `0`；与 `ps` 同源 |
| 演示 | `exec TASKMGR.ELF`；见课堂作业「如何写一个任务管理器」 |

---

## GUI（libUi **3.0.0** / libGraphics **3.0.0**）

| 签名 | 注意 |
| ---- | ---- |
| `UiCreateWindow(title,w,h)` | 成功 `wid≥0`；`<Ui.h>` |
| `UiSetLabel` / `UiAddButton(wid,id,label)` | `id` ∈ **0..3** |
| `UiPoll(wid)` | 见下表；非阻塞 |
| `GraphicsDamageText` / `GraphicsDamageRect` | `<Graphics.h>`；Rect 有尺寸上限 |

| `UiPoll` | 含义 |
| -------- | ---- |
| `UI_EVENT_NONE` (0) | 无事件 |
| `UI_EVENT_CLOSE` (1) | 关窗 |
| `UI_EVENT_CLICK` (2) | 客户区点击未命中控件 |
| `UI_BUTTON_EVENT(id)` | 按钮 |
| `UI_CHECK_EVENT` / `UI_LIST_EVENT` / `UI_TEXT_EVENT` | 复选 / 列表 / 输入 |
| `UI_KEY_EVENT(hid)` | 按键（方向 `0x4F`–`0x52`；Enter `0x28`；Esc `0x29`；…） |
| `-1` | 无效 wid |

```c
int w = UiCreateWindow("Hi", 320, 200);
UiAddButton(w, 0, "OK");
for (;;) {
    int ev = UiPoll(w);
    if (ev == UI_EVENT_CLOSE) break;
    if (ev == UI_BUTTON_EVENT(0)) { /* … */ }
}
```

---

## 网络（libNet **3.0.0**；Guest 先 `lwip on`）

| 签名 | 注意 |
| ---- | ---- |
| `socket` + `NetConnect`/`NetBind` | **主机序**；`<Net.h>`；不教 `sockaddr` |
| `NetResolve(name, &ip)` | 点分 / `localhost` / 域名 |
| `send` / `recv` | 字节数；失败负 / `-1` |

```c
int fd = socket(AF_INET, SOCK_STREAM, 0);
NetConnect(fd, NetIpv4(10,0,2,2), 7);
```

---

## 红字（必背）

1. **`fopen("w")` 不截断** → `remove` 再 `fopen`。  
2. **`ProcessWait`**：退出码在 CRT 里 `<<8`（无按 pid 等待）。  
3. **旧 GUI syscall 18/19/20/21 禁用**；只用 `Ui*` / `Graphics*`（1000+）。  
4. 未知 syscall 返回 **`-ENOSYS`（-38）**，不崩。  
5. 编译：**禁止** `-pie` / 宿主 libc；用 `Tools/Sdk/Examples` 或树内 User 规则。  
6. ELF 盘名：**8.3 大写**（`MYAPP.ELF`）；卷前缀 **`BLOCKS:`**。  
7. 日常构建：`source ~/Blocks/env.sh` → `build` / `run`。  
8. **勿用**公开 `toy_*` / 假 POSIX 双轨课设。
