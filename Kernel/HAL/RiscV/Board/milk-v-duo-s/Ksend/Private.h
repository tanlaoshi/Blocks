/*
 * Ksend/Private.h — Ksend 模块内部：协议常量、静态图、分文件函数
 *
 * 线上格式（小端）见 Ksend/README.md；
 * 块头 = magic/index/length/crc32 各 4 字节。
 */
#ifndef KSEND_PRIVATE_H
#define KSEND_PRIVATE_H

#include "Ksend.h"
#include "BootTypes.h"
#include "Fat.h"

/* —— 协议 —— */
/* 4 KB 块：与 FAT 簇大小解耦；PC 侧按同样粒度算差量 */
#define KSEND_BLOCK_BYTES         4096u
#define KSEND_HEADER_BYTES        16u
/* 线上字节 'K' 'B' 'S' 'K'（小端读成 0x4B53424B） */
#define KSEND_MAGIC               0x4B53424Bu
/* 收尾帧：index=全 F、length=0、crc=新文件整体 CRC32 */
#define KSEND_TRAILER_INDEX       0xFFFFFFFFu
/* v1 扩展：PC 主动放弃（--dry-run / Ctrl-C）。见 README「偏离」一节 */
#define KSEND_ABORT_INDEX         0xFFFFFFFEu
#define KSEND_ACK_WRITTEN         0x41u /* 'A'：已写卡 */
#define KSEND_ACK_RETRY           0x4Eu /* 'N'：重发同一块 */
/* 块表末尾的 1 字节 '\n'（表本身是二进制，不能用行结束符） */
#define KSEND_TABLE_END           0x0Au
#define KSEND_WORD_BYTES          4u
#define KSEND_BYTE_BITS           8u
#define KSEND_BYTE_MASK           0xFFu
/* 同一块连回 'N' 的上限（PC 侧同样按 3 次封顶） */
#define KSEND_RETRY_MAX           3u

/* —— 上限 —— */
/* 单文件上限取 FAT 层的 FAT_WRITE_MAX（Fat.h：8 MB）；块 CRC 表按它开静态数组 */
#define KSEND_MAX_BYTES           (8u * 1024u * 1024u)
#define KSEND_MAX_BLOCKS          (KSEND_MAX_BYTES / KSEND_BLOCK_BYTES)
/* 整文件读写分块：每次 FileSystem*FileAt 都要从首簇沿 FAT 链走到偏移，
 * 4 KB 一块读 2.2 MB 是 O(n²)；64 KB 一块把链步数压到 1/16 */
#define KSEND_IO_CHUNK_BYTES      (64u * 1024u)
#define KSEND_TEXT_BYTES          192u

/* —— 卡上路径 ——
 * 默认目标仍是 BLOCKS:Kernel.elf（Install.c 写的那个）；`ksend <路径>` 可换。
 * 备份名由目标名派生：同目录、换扩展名成 .BAK、全大写（Kernel.elf → KERNEL.BAK，
 * SNAKE.ELF → SNAKE.BAK），与 v1 的固定 KERNEL.BAK 保持同一形态。 */
#define KSEND_KERNEL_PATH         "BLOCKS:Kernel.elf"
#define KSEND_PATH_BYTES          64u
#define KSEND_BACKUP_EXTENSION    ".BAK"
#define KSEND_SELFTEST_PATH       "BLOCKS:KSELFT.BIN"
#define KSEND_SELFTEST_BYTES      8192u
/* 自检第二遍的盐：两遍内容必须不同，否则「覆写已有文件」这条路径测不出来 */
#define KSEND_SELFTEST_SALT       0x5Au
#define KSEND_FAIL_OFFSET_NONE    0xFFFFFFFFu

/* —— 超时（ms） —— */
/* UART 没起时别把壳挂死：写每字节最多等这么久 */
#define KSEND_TX_WAIT_MS          1000u
/* READY 之前要备份 + 算 CRC，PC 侧给 180 s；这边等块头给 30 s */
#define KSEND_HEADER_TIMEOUT_MS   30000u
/* 115200 下一块 4 KB 约 356 ms；逐字节 5 s 足够 */
#define KSEND_DATA_TIMEOUT_MS     5000u
/* 发 READY 前吃掉命令行尾巴：静默 20 ms 即认为干净 */
#define KSEND_DRAIN_QUIET_MS      20u
#define KSEND_DRAIN_LIMIT_MS      500u
/* 失败后可能还有 PC 的 abort / 重发块在路上，窗口放长一点 */
#define KSEND_FAIL_DRAIN_QUIET_MS 100u
#define KSEND_FAIL_DRAIN_LIMIT_MS 3000u

/* —— CRC-32/ISO-HDLC（与 Python zlib.crc32 同参，反射型） —— */
#define KSEND_CRC_POLYNOMIAL      0xEDB88320u
#define KSEND_CRC_INITIAL         0xFFFFFFFFu
#define KSEND_CRC_FINAL_XOR       0xFFFFFFFFu
#define KSEND_CRC_BITS_PER_BYTE   8u

/* —— 失败分类（KSEND FAIL reason=…） —— */
#define KSEND_FAIL_NONE    0
#define KSEND_FAIL_STAT    1 /* stat Kernel.elf 失败 */
#define KSEND_FAIL_BACKUP  2 /* BAK 复制或回读校验失败 */
#define KSEND_FAIL_HEADER  3 /* 块头非法 / 等块超时 */
#define KSEND_FAIL_WRITE   4 /* 落盘失败（重试超限） */
#define KSEND_FAIL_VERIFY  5 /* 块 CRC 或整体 CRC 不符 */
#define KSEND_FAIL_FLUSH   6 /* FileSystemFileSync 失败 */
#define KSEND_FAIL_SHRINK  7 /* 补零失败 */
#define KSEND_FAIL_EMPTY   8 /* 一个块都没收到 */
#define KSEND_FAIL_ABORT   9 /* PC 主动放弃 */

/* 一个文件的分块图：整文件 CRC + 每 4 KB 一块的 CRC */
typedef struct {
    UINT32 Size;
    UINT32 BlockCount;
    UINT32 Crc;
    UINT32 BlockCrc[KSEND_MAX_BLOCKS];
} KSEND_IMAGE;

/* 一次传输的现场：收块状态机与收尾/回滚两边共用 */
typedef struct {
    const char *TargetPath; /* 卡上目标（默认 BLOCKS:Kernel.elf） */
    const char *BackupPath; /* 同目录 <目标主名>.BAK */
    UINT32 OldSize;
    UINT32 NewSize;
    UINT32 TrailerCrc;
    UINT32 Written;
    UINT32 FailBlock;
    UINT32 FailWant;
    UINT32 FailGot;
    int Dirty;   /* 只要尝试过写卡就置 1：失败的写也可能改了半块，回滚判据用它 */
    int Failure;
    int Err;
} KSEND_RUN;

/* 自检结果（给 KsendSelftest 打印用） */
typedef struct {
    UINT32 Size;
    UINT32 Crc;
    UINT32 Step;       /* 1=新建，2=覆写 */
    UINT32 FailOffset;
    UINT32 FailWant;
    UINT32 FailGot;
} KSEND_SELFTEST;

/* 小端文本拼装（KsendText.c） */
typedef struct {
    char *Buffer;
    int Capacity;
    int Length;
} KSEND_TEXT;

/* —— Ksend.c —— */
void KsendNote(const char *Text);
int KsendInfo(void);
int KsendSelftest(void);
int KsendTransfer(const char *TargetPath);

/* —— KsendSerial.c —— */
void KsendSerialWrite(const char *Text);
void KsendSerialPutByte(UINT8 Value);
int KsendSerialGetByte(UINT8 *Out, UINT32 TimeoutMs);
/* 吃掉 RX 上没人认领的字节：静默 QuietMs 或总时长 LimitMs 到就停 */
void KsendSerialDrainInput(UINT32 QuietMs, UINT32 LimitMs);
/* 等移位寄存器发空（TEMT），复位前必须调用，否则 KSEND OK 可能被截断 */
void KsendSerialDrainTransmitter(void);
/* 25 MHz rdtime 换算的毫秒计数 / 忙等（复位时序用） */
UINT32 KsendMilliseconds(void);
void KsendDelayMs(UINT32 Milliseconds);

/* —— KsendText.c —— */
void KsendTextReset(KSEND_TEXT *Text, char *Buffer, int Capacity);
void KsendTextPut(KSEND_TEXT *Text, const char *String);
void KsendTextPutUnsigned(KSEND_TEXT *Text, UINT32 Value);
void KsendTextPutHex(KSEND_TEXT *Text, UINT32 Value);
void KsendTextPutField(KSEND_TEXT *Text, const char *Key, UINT32 Value);
void KsendTextPutHexField(KSEND_TEXT *Text, const char *Key, UINT32 Value);
void KsendTextPutSignedField(KSEND_TEXT *Text, const char *Key, int Value);
void KsendTextFlush(KSEND_TEXT *Text);

/* —— KsendCrc.c —— */
UINT32 KsendCrc32Update(UINT32 Crc, const UINT8 *Data, UINT32 Length);
UINT32 KsendCrc32Final(UINT32 Crc);

/* —— KsendReport.c（协议行 / 人话行） —— */
void KsendPrintImage(const char *Tag, const KSEND_IMAGE *Image);
void KsendPrintReason(const char *Tag, const char *Reason, int Err);
void KsendReadyLine(const KSEND_IMAGE *Image);
void KsendBlockTable(const KSEND_IMAGE *Image);
void KsendOkLine(UINT32 Crc, UINT32 Written, UINT32 Size);
void KsendFailLine(int Failure, UINT32 Block, UINT32 Want, UINT32 Got, int Err);
void KsendAbortLine(UINT32 Written);
void KsendRestoredLine(const KSEND_IMAGE *Image);

/* —— KsendReceive.c —— */
/* 收块直到收尾帧：0=收到收尾（Run->NewSize/TrailerCrc 已就绪）；-1=失败/放弃 */
int KsendReceiveBlocks(KSEND_RUN *Run);

/* —— KsendFile.c —— */
/* 旧文件（备份）图：restore 的判据 */
extern KSEND_IMAGE gKsendOldImage;
/* 传输中「期望的新内容」图；info 也拿它当只打印的草稿 */
extern KSEND_IMAGE gKsendNewImage;

/* 64 KB 复用缓冲（自检也用它，省一份静态数据） */
UINT8 *KsendFileBuffer(void);
int KsendFileDelete(const char *Path);
int KsendFileSize(const char *Path, UINT32 *OutSize);
/* 写在任意偏移（FAT 层按需扩簇；0 长度由调用方避免） */
int KsendFileWriteAt(const char *Path, UINT32 Offset, const UINT8 *Data,
                     UINT32 Length);
/* 顺序读整文件，填 Size/BlockCount/Crc/BlockCrc */
int KsendFileScan(const char *Path, UINT32 Size, KSEND_IMAGE *Image);
/* 复制并顺带算出被复制内容的图（源先读、目标先删） */
int KsendFileCopy(const char *Source, const char *Target, UINT32 Size,
                  KSEND_IMAGE *Image);
/* 写一块（Path 的 Index*4096 起，Length 1..4096） */
int KsendFileWriteBlock(const char *Path, UINT32 Index, const UINT8 *Data,
                        UINT32 Length);
/* 把 [Offset, Offset+Length) 写成 0（FAT 无 truncate 时的兜底） */
int KsendFileZeroRange(const char *Path, UINT32 Offset, UINT32 Length);
/* 1=一致；0=块不符（OutBlock/Want/Got）；-1=整体 CRC 不符 */
int KsendFileVerify(const char *Path, UINT32 Size, const KSEND_IMAGE *Expected,
                    UINT32 ExpectedCrc, UINT32 *OutBlock, UINT32 *OutWant,
                    UINT32 *OutGot);
/* 删目标再从备份重写并校验（尺寸与内容都回到备份） */
int KsendFileRestore(const char *BackupPath, const char *TargetPath,
                     const KSEND_IMAGE *Expected);
int KsendFileSelfTest(KSEND_SELFTEST *Out);

/* —— KsendReset.c —— */
/* 复位（SG2000 RTC 温复位）。复位成功则不返回；返回前会打印诊断并停车 */
void KsendReboot(void);

#endif /* KSEND_PRIVATE_H */
