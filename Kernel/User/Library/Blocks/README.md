# Library/Blocks · 自有用户态门面

**不是 POSIX 兼容层，不做双轨。** 与 ISO 的 [`../Libc/`](../Libc/) 并列。

| 目录 | 内容 |
| --- | --- |
| `start/` | `_start`、syscall、`call_main` |
| `host/` | 文件/进程/cwd/sleep/`brk`/映射等 Blocks 门面（原散落实现） |
| `dir/` | 目录枚举（`Dir*`） |
| `thread/` | 线程 / 调度协作 |
| `socket/` | 套接字薄封装 |
| `stat/` | 文件状态 |

产物：`libblocks.a`；链接另加 `start/crt0.o` + `start/syscall.o` + `../Libc/libc.a`。  
缺口与 C99 矩阵：[`Archive/项目/LibcGaps.md`](../../../../Archive/项目/LibcGaps.md)。
