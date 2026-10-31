# Ksend — 串口增量写任意文件（Duo S 开发期工具，v2）

目标：改完内核不用拔卡，PC 只发和卡上不一样的那几块，8 秒级一轮，写完自动复位。
v2 起目标不再写死 `Kernel.elf`：`--file` 是本地文件，`--path` 是卡上目标，
板端对应 `ksend <路径>`；两边都不给路径时仍是 `BLOCKS:Kernel.elf`。

```
PC:  python3 Tools/ksend.py --port /dev/ttyUSB0 --file Kernel/Build/DUO/HAL/RiscV/Kernel.elf
PC:  python3 Tools/ksend.py --port /dev/ttyUSB0 --file SNAKE.ELF --path BLOCKS:SNAKE.ELF
板:  blocks> ksend info               # 只读：Kernel.elf 的 size / n / 整体 CRC（行为不变）
板:  blocks> ksend selftest           # 只写 8 KB 临时文件，判 FAT 写路径（行为不变）
板:  blocks> ksend                    # 完整传输，写 Kernel.elf（回归）
板:  blocks> ksend BLOCKS:SNAKE.ELF   # 完整传输，写指定文件
```

## 线上格式（小端）

| 方向 | 内容 |
| ---- | ---- |
| PC → 板 | `ksend\n`（默认路径）或 `ksend <目标路径>\n`（板端 shell 参数） |
| 板 → PC | `KSEND READY size=<旧大小> block=4096 n=<旧块数>\r\n` |
| 板 → PC | n 个 `u32` 旧块 CRC32，然后 1 字节 `0x0A` |
| PC → 板 | 每个差异块：`u32 magic=0x4B53424B` `u32 index` `u32 len` `u32 crc32(data)` + data |
| 板 → PC | `'A'` 已写卡 / `'N'` 请重发同一块（PC 最多重发 3 次） |
| PC → 板 | 收尾：`u32 magic` `u32 0xFFFFFFFF` `u32 0` `u32 新文件整体 CRC32` |
| 板 → PC | `KSEND OK crc=… written=… size=…`（随后复位）或 `KSEND FAIL …`（不复位） |

CRC 是 CRC-32/ISO-HDLC，和 Python `zlib.crc32` 同参。

## 安全顺序（顺序不能换）

1. `ksend selftest` 先过：8 KB 临时文件，新建一遍 + 覆写一遍，读回逐字节比对，删除。
2. 真传第一步：`<目标>` → 同目录 `<目标主名>.BAK`（64 KB 分块走 FileSystem API），
   回读校验 size + 整体 CRC。备份不过就 `KSEND FAIL reason=backup`，一个字节都不写。
   备份名规则：换扩展名、主名全大写（`Kernel.elf` → `KERNEL.BAK`，`SNAKE.ELF` → `SNAKE.BAK`），
   与 v1 的 `KERNEL.BAK` 同名同形态。
3. 直接写目标文件（按 4 KB 块落在 `index*4096`），每块写完回 `'A'`。
4. 收尾：`FileSystemFileSync`（→ `BlockFlush`）后重新打开文件逐块读回，
   块 CRC 对齐预期、整体 CRC 对齐收尾帧。
5. 只有校验通过才 `KSEND OK` 并复位；任何失败都 `KSEND FAIL`，**不复位**；
   只要尝试过写卡（含写失败），就用备份删了重写并回读校验，打印 `KSEND RESTORED`。

## v1/v2 与需求文本的出入（都是刻意的）

1. **收尾帧没有 size 字段**，板子只能用「收到过的最大 `index*4096+len`」推新大小。
   因此 PC 侧**永远重发最后一块**（哪怕它没变）；否则「新文件变短 / 末块没变」
   时会把新尺寸推小。PC 与板子都按这条写。
2. **多了 abort 帧**：`index=0xFFFFFFFE, len=0`。只给 `--dry-run` 和 Ctrl-C 用，
   让板子立刻收手（没写过卡 → `KSEND ABORT`；写过 → 先回滚再 ABORT）。
   协议里本来没有「PC 不发了」这个信号。
3. `KSEND FAIL` 只在块 CRC 不符时用 `at=/want=/got=`；I/O 失败用
   `reason=write at=<块> err=<代码>`——此时 want 与 got 相等，套规范那三个字段反而误导。
4. `info` / `selftest` 额外打一行人类可读文本；机器行分别是
   `KSEND INFO size=… block=4096 n=… crc=…` 和 `KSEND SELFTEST OK|FAIL …`。
5. v2 的路径只走**命令参数**，不塞进二进制帧：PC 发的 `ksend <路径>` 就是板端
   shell 命令行。因此路径里不能有空格/控制字符，长度上限 63（板端 64 B 缓冲）。
   `--info` 仍只查 `Kernel.elf`（行为不变）。

## 已知限制

- **FAT 层没有 truncate**（只调 `FileSystem.h` 公开 API）。新文件比旧文件小时，
  [新大小, 旧大小) 会被写成 0，但目录项尺寸仍是旧值：目标文件带 0 尾巴。
  ELF 装载按 program header 取，尾巴不参与；下一次 `ksend info` 看到的是旧尺寸，
  下次传输也照常工作（多出来的块永远是 0）。真要精确尺寸只能整文件重写。
- 单文件上限 `FAT_WRITE_MAX` = 8 MB（FAT 层限制）。
- 串口固定 115200：内核不重设波特率，U-Boot 设成什么就是什么。
- 传输窗口内掉电会留下「半新半旧」的目标文件：板子自己起不来，只能读卡器
  把 `<目标主名>.BAK` 改名回去。备份是唯一的救生圈，别删。
