/*
 * Ksend/Private.h — Ksend 模块内部：协议常量、静态图、分文件函数
 *
 * 线上格式（小端）见 Ksend/README.md；
 * 块头 = magic/index/length/crc32 各 4 字节。
 */
#ifndef KSEND_PRIVATE_H
#define KSEND_PRIVATE_H

#include "Hal/Ksend.h"
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
/* 整文件读写分块：大 chunk 压 FileAt 次数。Seek/扩链顺序游标见
 * FileWriteAtCluster.c（PR-KSEND-FAT-CURSOR）。接收与扫描/备份共用本缓冲。 */
/*
 * IO 复用缓冲（扫描/备份/收块攒批）。有线吞吐：按 win 攒批；落盘后再齐发 'A'。
 */
#define KSEND_IO_CHUNK_BYTES      (128u * 1024u)
/* TCP 滑窗：READY win=；串口/Wi‑Fi=1；有线 TCP 开窗（nuc-1 → WIN16） */
#define KSEND_WINDOW_SERIAL       1u
#define KSEND_WINDOW_WIFI         1u
#define KSEND_WINDOW_WIRED_TCP    16u
#define KSEND_WINDOW_MAX          16u
#define KSEND_TEXT_BYTES          256u /* OK 行含 net/flush/verify/install */

/* —— 差量表缓存 <目标全名>.crc（Cache.c）——
 * 字段固定 6×u32 + block_count×u32（全部小端），总大小 24+4n B。
 * magic 按协议给定数值 0x4B534352（小端写出的字节是 52 43 53 4B）。
 * 它只是加速缓存：任何时刻删掉都必须能退回逐块读文件的慢路径。 */
#define KSEND_CACHE_MAGIC          0x4B534352u /* "KSCR"（协议号） */
#define KSEND_CACHE_VERSION        1u
#define KSEND_CACHE_EXTENSION      ".crc"
#define KSEND_CACHE_HEADER_BYTES   24u
#define KSEND_CACHE_OFFSET_MAGIC   0u
#define KSEND_CACHE_OFFSET_VERSION 4u
#define KSEND_CACHE_OFFSET_BLOCK_BYTES 8u
#define KSEND_CACHE_OFFSET_BLOCK_COUNT 12u
#define KSEND_CACHE_OFFSET_TOTAL_SIZE  16u
#define KSEND_CACHE_OFFSET_FULL_CRC    20u
/* 最大 8 MB 文件 → 2048 块 → 8216 B 缓存；超过就当坏文件拒绝，不整读 */
#define KSEND_CACHE_MAX_BYTES \
    (KSEND_CACHE_HEADER_BYTES + KSEND_WORD_BYTES * KSEND_MAX_BLOCKS)

/* —— 卡上路径 ——
 * 默认目标仍是 BLOCKS:Kernel.elf（Install.c 写的那个）；`ksend <路径>` 可换，
 * 前缀支持 BLOCKS: / BOOT: / ESP: / 卷字母 / 卷下标。
 * 备份名 = 目标全名追加 .bak（boot.sd → boot.sd.bak），同目录、不换扩展名；
 * 保留原大小写，FAT 名字比较本来忽略大小写。 */
#define KSEND_KERNEL_PATH         "BLOCKS:Kernel.elf"
#define KSEND_PATH_BYTES          96u
#define KSEND_BACKUP_EXTENSION    ".bak"
/* 覆写已有目标时先落到同目录 <目标>.new（空文件全量收），校验后再改名替换；
 * 避免对正在跑的 Kernel.elf 做 WriteAt（有线大文件覆写会挂，新建同尺寸则通）。 */
#define KSEND_STAGING_EXTENSION   ".new"
#define KSEND_SELFTEST_PATH       "BLOCKS:KSELFT.BIN"
#define KSEND_SELFTEST_BYTES      8192u
/* 自检第二遍的盐：两遍内容必须不同，否则「覆写已有文件」这条路径测不出来 */
#define KSEND_SELFTEST_SALT       0x5Au
#define KSEND_FAIL_OFFSET_NONE    0xFFFFFFFFu

/* —— 超时（毫秒） —— */
/* UART 没起时别把壳挂死：写每字节最多等这么久 */
#define KSEND_TRANSMIT_WAIT_MILLISECONDS          1000u
/* READY 之前要备份 + 算 CRC，PC 侧给 180 s；这边等块头给 30 s */
#define KSEND_HEADER_TIMEOUT_MILLISECONDS   30000u
/* 115200 下一块 4 KB 约 356 毫秒；逐字节 5 秒足够 */
#define KSEND_DATA_TIMEOUT_MILLISECONDS     5000u
/* 发 READY 前吃掉命令行尾巴：静默 20 毫秒即认为干净 */
#define KSEND_DRAIN_QUIET_MILLISECONDS      20u
#define KSEND_DRAIN_LIMIT_MILLISECONDS      500u
/* 失败后可能还有 PC 的 abort / 重发块在路上，窗口放长一点 */
#define KSEND_FAIL_DRAIN_QUIET_MILLISECONDS 100u
#define KSEND_FAIL_DRAIN_LIMIT_MILLISECONDS 3000u

/* —— CRC-32/ISO-HDLC（与 Python zlib.crc32 同参，反射型） —— */
#define KSEND_CRC_POLYNOMIAL      0xEDB88320u
#define KSEND_CRC_INITIAL         0xFFFFFFFFu
#define KSEND_CRC_FINAL_XOR       0xFFFFFFFFu
#define KSEND_CRC_BITS_PER_BYTE   8u

/* —— 失败分类（KSEND FAIL reason=…） —— */
#define KSEND_FAIL_NONE    0
#define KSEND_FAIL_FILE_STATUS    1 /* stat Kernel.elf 失败 */
#define KSEND_FAIL_BACKUP  2 /* BAK 复制或回读校验失败 */
#define KSEND_FAIL_HEADER  3 /* 块头非法（magic/字段） */
#define KSEND_FAIL_WRITE   4 /* 落盘失败（重试超限） */
#define KSEND_FAIL_VERIFY  5 /* 块 CRC 或整体 CRC 不符 */
#define KSEND_FAIL_FLUSH   6 /* FileSystemFileSync 失败 */
#define KSEND_FAIL_SHRINK  7 /* 补零失败 */
#define KSEND_FAIL_EMPTY   8 /* 一个块都没收到 */
#define KSEND_FAIL_ABORT   9 /* PC 主动放弃 */
#define KSEND_FAIL_SEND   10 /* TCP/串口发送停滞（Wi‑Fi 表发死等） */
#define KSEND_FAIL_DATA   11 /* 块头后数据字节超时/短读（旧码曾误报 header） */

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
    const char *BackupPath; /* 同目录 <目标全名>.bak */
    const char *WritePath;  /* 实际落盘路径；覆写时为 Target.new，否则同 Target */
    UINT32 OldSize;
    UINT32 NewSize;
    UINT32 TrailerCrc;
    UINT32 Written;
    UINT32 FailBlock;
    UINT32 FailWant;
    UINT32 FailGot;
    int VolumeIndex; /* 本次 SelectVolume 选中的卷；-1 = 未选（不用归还） */
    int Reboot;   /* 1=成功且校验通过后复位（默认）；0=--no-reboot 留 shell */
    int Backup;   /* 1=Install 时 Target→.bak 改名（默认）；0=--no-backup 删 Target */
    int Dirty;   /* 只要尝试过写卡就置 1：失败的写也可能改了半块，回滚判据用它 */
    int Failure;
    int Error;
    /* 阶段墙钟（PHASE-METRICS / PHASE-FLUSH）：毫秒，失败路径可部分为 0 */
    UINT32 PrepareMilliseconds;
    UINT32 ReadyTableMilliseconds;
    UINT32 ReceiveMilliseconds;
    UINT32 NetMilliseconds;    /* receive 内：收窗/CRC/ACK（非 WriteAt） */
    UINT32 FlushMilliseconds;  /* receive 内：KsendFileWriteAt 累计 */
    UINT32 FinishMilliseconds;
    UINT32 VerifyMilliseconds; /* finish 内：sync + 盘回读校验 */
    UINT32 InstallMilliseconds; /* finish 内：staging 改名 + .crc */
    UINT32 TotalMilliseconds;
} KSEND_RUN;

/* 自检结果（给 KsendSelfTest 打印用） */
typedef struct {
    UINT32 Size;
    UINT32 Crc;
    UINT32 Step;       /* 1=新建，2=覆写 */
    UINT32 FailOffset;
    UINT32 FailWant;
    UINT32 FailGot;
} KSEND_SELFTEST;

/* 小端文本拼装（Text.c） */
typedef struct {
    char *Buffer;
    int Capacity;
    int Length;
} KSEND_TEXT;


#include "Ksend/PrivateDeclarations.h"

#endif /* KSEND_PRIVATE_H */
