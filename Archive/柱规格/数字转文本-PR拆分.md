# 数字转文本进 Library — PR 拆分

> 状态：待看。还没改代码。
>
> 目标：把「数字写成文本」从 PCI、串口、设备列表、网卡里收进 `Library`。调用方不再为了打一个数字去包含 `PCIe.h`。
>
> 只搬家。每条已经在打印的字符串保持原样，包括 `0x`、大小写、有没有前导零。

## 现在的代码

| 位置 | 现状 |
| --- | --- |
| `HAL/X64/PCIe/PCIe.c` | `Uint8ToDecimal`、`Uint32ToHex`、`Uint64ToHex`。都返回函数自己的静态缓冲区。声明在 `PCIe.h` |
| `Drivers/Net/NetAddr.c` `NetInfo` | 调用 `Uint8ToDecimal`，没有包含 `PCIe.h`。编译器把它当成返回 `int` |
| `Drivers/Serial/Serial.c` `SerialHexFormat` | 调用者缓冲。`0x` + 大写，位数由参数决定。x64 `HalSerialFormatHex` 只转调它。`Ehci/Hw.c` 依赖「前两个字符是 `0x`，数字从下标 2 起」 |
| `HAL/Arm64/Hal/HalSerial.c`、`HAL/RiscV/Hal/HalSerial.c` | 各自一份 `HalSerialFormatHex`：小写、没有 `0x`、位数 1～16，参数不合法就直接返回 |
| `Library/LibWrite.c` `LibWriteHexDigits` | 已经在 Library。小写、没有 `0x`，写进输出槽，不返回字符串 |
| `DeviceDump.c`、`DeviceTreeDump.c`、`DevicesUiModel.c`、`DevicesUiPaint.c` | 各有一份 `static HexDigit`，小写半字节 |
| `Net/NetAddr.c` `NetFormatIp`、`HAL/Virt/VirtioNet/VirtioNetAddr.c` `VirtioNetFormatIp` | 同一套四段十进制加点。`HalNetFormatIp` 只转给当前网卡后端，不是第三份算法 |

## 读完应有的样子

单文件，不满 300 行，不为它建目录。`Kernel/Makefile` 的 `Library/*.c` 会自己收进去。

```text
Kernel/Library/Text.c
Kernel/Include/Library/Text.h
```

头只包含 `BootTypes.h`。实现不包含 Hal / Core / Services，不调用 `Hal*`。`make check-library-pure` 要过。

写入调用者缓冲的新函数（全词，名字不以文件名 `Text` 开头）：

```c
/* 无前导零。放不下则当 Capacity > 0 时写成空串，不写出界 */
void FormatUnsignedDecimal(char *Buffer, int Capacity, UINT64 Value);

/* Digits 个半字节。Uppercase 非 0 用 A-F，否则 a-f。WithPrefix 非 0 先写 "0x"。
   放不下则当 Capacity > 0 时写成空串，不写出界 */
void FormatHexadecimal(char *Buffer, int Capacity, UINT64 Value,
                       int Digits, int Uppercase, int WithPrefix);

/* 低 4 位，小写一个字符 */
char FormatHexadecimalDigit(UINT32 Value);

/* 高字节在前，每段无前导零，点连接。放不下则当 Capacity > 0 时写成空串 */
void FormatDottedDecimal(UINT32 Value, char *Buffer, int Capacity);
```

三个旧名留在 `Text.h`，仍返回静态缓冲，字符和现在逐字相同。名字是简写，只为已经在用的调用点，新代码用上面四个：

```c
char *Uint8ToDecimal(UINT8 Value);   /* 1～3 位十进制 */
char *Uint32ToHex(UINT32 Value);     /* "0x" + 8 位大写 */
char *Uint64ToHex(UINT64 Value);     /* "0x" + 16 位大写 */
```

`HalSerialFormatHex`、`SerialHexFormat`、`NetFormatIp`、`VirtioNetFormatIp`、`HalNetFormatIp` 的名字和声明都不改。成百处诊断日志继续调原来的函数。

## 明确不做

- 不改诊断串（`XhciDiagFormat`、`Ps2DiagFormat` 这一类）。它们拼的是设备状态，不是通用转换。
- 不改 `FormatNowDisplay` 等界面专用格式。
- 不把 `LibWrite` / `LibWriteHex32` / `LibWriteHex64` 的对外行为改掉。刀 2 只让内部半字节走同一份循环。
- 不统一三套架构的十六进制外观。x64 继续 `0x` + 大写；Arm64 / RiscV 继续小写、无前缀。
- 不修静态缓冲的重入。同一次调用里写两次 `Uint64ToHex` 仍会共用一块缓冲，和现在一样。
- 不给 `SerialHexFormat` 新加空指针或位数检查。它仍假定缓冲至少 `2 + Digits + 1` 字节。

## 刀

| 序 | PR | 改哪些 | 本刀不碰 | 验收 |
| --- | --- | --- | --- | --- |
| 1 | **PR-TEXT-move** | 新增 `Text.c` / `Text.h`。三个旧函数原样搬入，字符串不变。`PCIe.c` 删实现，`PCIe.h` 删声明。`PCIe.c`、`NetAddr.c` 包含 `Text.h` | 串口十六进制、`HexDigit`、IP 文本、`LibWrite` | 三架构 `./build.sh`。`NetInfo` 不再把 `Uint8ToDecimal` 当成返回 `int` |
| 2 | **PR-TEXT-hex** | `SerialHexFormat` 改为调用 `FormatHexadecimal`（大写、带 `0x`）。Arm64 / RiscV 的 `HalSerialFormatHex` 改为调用它（小写、不带 `0x`，非法参数仍直接返回）。`LibWriteHexDigits` 改为调用它（小写、不带 `0x`）。四个 `static HexDigit` 改为 `FormatHexadecimalDigit` | 调用点名字、`Ehci/Hw.c` 对下标 2 的用法、IP 文本 | 三架构 `./build.sh`。x64 串口十六进制仍以 `0x` 开头且为大写 |
| 3 | **PR-TEXT-address** | `NetFormatIp` 与 `VirtioNetFormatIp` 在各自现有的长度判断之后调用 `FormatDottedDecimal`。短缓冲、空指针时仍按现在各自的写法返回，不多写一个 `0` | `HalNetFormatIp` 的转发、IP 解析、收发 | 三架构 `./build.sh`。`"10.0.2.15"` 这种无前导零的写法不变 |

## 建议顺序

```text
PR-TEXT-move → PR-TEXT-hex → PR-TEXT-address
```

刀 1 先让 PCI 和网卡信息不再依赖 `PCIe.h`。刀 2 收十六进制。刀 3 收点分十进制，并复用刀 1 的十进制写法。
