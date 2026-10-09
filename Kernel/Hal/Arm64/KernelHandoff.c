/*
 * KernelHandoff.c — Arm64：填写 BOOT_INFO，再调用 KernelMain
 *
 * 【初学者】
 * 没有 UEFI。QEMU `-kernel` 把 ELF 装进内存，KernelEntry 把 DTB 物理址
 * 交给本函数。我们尽量从 DTB 读 memory 节点；失败则退回 virt 约定：
 *   RAM @ 0x40000000，大小 256MiB。
 *
 * 源码在 Kernel/Hal（链进 Kernel.elf），不是独立 Boot 镜像。
 * 不链接完整 libfdt——下面有一段教学向的极简扫描即可。
 *
 * 填完 BOOT_INFO → HalSerial 横幅 → KernelMain(&Info)。
 */
#include "BootInfoTypes.h"
#include "Kernel.h"
#include "HalSerial.h"
#include "BoardConfig.h"

/* 链接脚本：内核映像末尾（已加载进内存的物理地址） */
extern char __kernel_end[];

#define ARM64_VIRT_DTB_ADDR 0x4a000000ULL          /* QEMU virt 常见 DTB 落点 */
#define ARM64_VIRT_RAM_BASE 0x40000000ULL          /* virt RAM 起点 */
#define ARM64_VIRT_RAM_SIZE (256ULL * 1024ULL * 1024ULL) /* DTB 失败时的回退大小 */
#define FDT_MAGIC 0xd00dfeedu                      /* Flattened Device Tree 魔数（大端） */

/* 无 libc：手写清零 */
static void MemZero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    UINTN i;
    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}

/* FDT 字段为大端；本机 aarch64 小端，读头/属性前先翻成主机序 */
static UINT32 Be32(const void *P) {
    const UINT8 *B = (const UINT8 *)P;
    return ((UINT32)B[0] << 24) | ((UINT32)B[1] << 16) |
           ((UINT32)B[2] << 8) | (UINT32)B[3];
}

/*
 * DtbMemoryRegion — 极简设备树扫描：只找「memory」或「memory@…」节点的 reg
 *
 * 为何自写、不引 Kernel/Hal/Dtb.h？
 *   Boot 须独立可编；这里只要一段 (Base, Size) 填进 BOOT_INFO。
 *
 * 返回：0 成功写出 *OutBase / *OutSize；-1 魔数不对 / 结构坏 / 找不到 memory。
 *
 * 简化点（教学可接受）：
 *   - 只认根附近的 memory 节点名；
 *   - #address-cells / #size-cells 遇到就更新（可能来自父节点 prop）；
 *   - 多段 memory 时后写覆盖前写（virt 通常一段）。
 */
static int DtbMemoryRegion(UINT64 DtbPhys, UINT64 *OutBase, UINT64 *OutSize) {
    const UINT8 *Blob;
    UINT32 Total, StructOff, StructSize, StringsOff, Off, End;
    UINT32 AddrCells = 2, SizeCells = 1; /* 常见默认；遇 prop 再改 */
    int HaveMem = 0;
    UINT64 MemBase = 0, MemSize = 0;

    if (!OutBase || !OutSize || DtbPhys == 0) {
        return -1;
    }
    Blob = (const UINT8 *)(UINTN)DtbPhys;
    if (Be32(Blob) != FDT_MAGIC) {
        return -1;
    }

    /* FDT 头：totalsize / off_dt_struct / off_dt_strings / size_dt_struct */
    Total = Be32(Blob + 4);
    StructOff = Be32(Blob + 8);
    StructSize = Be32(Blob + 36);
    StringsOff = Be32(Blob + 12);
    if (Total < 40 || StructOff >= Total || StringsOff >= Total ||
        StructSize == 0 || StructOff + StructSize > Total) {
        return -1;
    }

    Off = StructOff;
    End = StructOff + StructSize;
    while (Off + 4 <= End) {
        UINT32 Token = Be32(Blob + Off);
        Off += 4;

        if (Token == 1u) { /* FDT_BEGIN_NODE：后跟以 NUL 结尾的节点名，再 4 字节对齐 */
            const char *Name = (const char *)(Blob + Off);
            UINT32 Len = 0;
            int IsMem;
            while (Off + Len < End && Name[Len]) {
                Len++;
            }
            Off = (Off + Len + 1 + 3u) & ~3u;

            /* 名字以 memory 或 memory@ 开头则当作内存节点 */
            IsMem = (Name[0] == 'm' && Name[1] == 'e' && Name[2] == 'm' &&
                     Name[3] == 'o' && Name[4] == 'r' && Name[5] == 'y' &&
                     (Name[6] == 0 || Name[6] == '@'));
            if (!IsMem) {
                continue;
            }

            /* 在本节点内扫 prop，直到 END_NODE */
            while (Off + 4 <= End) {
                UINT32 T2 = Be32(Blob + Off);
                Off += 4;
                if (T2 == 2u) { /* FDT_END_NODE */
                    break;
                }
                if (T2 == 3u) { /* FDT_PROP：len, nameoff, value… */
                    UINT32 PLen = Be32(Blob + Off);
                    UINT32 PName = Be32(Blob + Off + 4);
                    const char *Pstr = (const char *)(Blob + StringsOff + PName);
                    const UINT8 *Val = Blob + Off + 8;
                    Off = (Off + 8 + PLen + 3u) & ~3u;

                    /* 粗匹配属性名：#address-cells / #size-cells / reg */
                    if (Pstr[0] == '#' && Pstr[1] == 'a') {
                        AddrCells = Be32(Val);
                    } else if (Pstr[0] == '#' && Pstr[1] == 's') {
                        SizeCells = Be32(Val);
                    } else if (Pstr[0] == 'r' && Pstr[1] == 'e' && Pstr[2] == 'g' &&
                               Pstr[3] == 0) {
                        UINT32 Need = (AddrCells + SizeCells) * 4u;
                        UINT32 i;
                        if (PLen < Need) {
                            continue;
                        }
                        /* reg = <addr-cells 个 cell 的 base> <size-cells 个 cell 的 size> */
                        MemBase = 0;
                        for (i = 0; i < AddrCells; i++) {
                            MemBase = (MemBase << 32) | Be32(Val + i * 4);
                        }
                        MemSize = 0;
                        for (i = 0; i < SizeCells; i++) {
                            MemSize = (MemSize << 32) | Be32(Val + (AddrCells + i) * 4);
                        }
                        HaveMem = 1;
                    }
                    continue;
                }
                if (T2 == 4u) { /* FDT_NOP */
                    continue;
                }
                if (T2 == 9u) { /* FDT_END */
                    break;
                }
                return -1; /* 未识别 token：宁可不信这份 DTB */
            }
            continue;
        }
        if (Token == 2u || Token == 4u) { /* 其它节点的 END_NODE / NOP */
            continue;
        }
        if (Token == 9u) { /* FDT_END */
            break;
        }
        if (Token == 3u) { /* 根级 PROP：跳过 */
            UINT32 PLen = Be32(Blob + Off);
            Off = (Off + 8 + PLen + 3u) & ~3u;
            continue;
        }
        return -1;
    }

    if (!HaveMem || MemSize == 0) {
        return -1;
    }
    *OutBase = MemBase;
    *OutSize = MemSize;
    return 0;
}

/* 往 Info->Regions[] 追加一段；满了或 Size==0 则失败 */
static int BootInfoAddRegion(BOOT_INFO *Info, UINT64 Phys, UINT64 Size, int Free) {
    if (Info->RegionCount >= BOOT_MEMORY_REGIONS_MAX || Size == 0) {
        return -1;
    }
    Info->Regions[Info->RegionCount].Phys = Phys;
    Info->Regions[Info->RegionCount].Size = Size;
    Info->Regions[Info->RegionCount].Free = Free ? 1u : 0u;
    Info->RegionCount++;
    return 0;
}

#if BRINGUP

/*
 * 极早期 bringup：证明能进 C 即可，不组 BOOT_INFO、不进 KernelMain。
 * 编译：build.sh / Makefile 定义 BRINGUP=1。
 */
void KernelHandoff(UINT64 DtbPhys) {
    (void)DtbPhys;
    HalSerialInitialize();
    HalSerialWrite("board: ");
    HalSerialWrite(BOARD_NAME);
    HalSerialWrite(" (bringup)\n");
    for (;;) {
    }
}

#else

/*
 * KernelHandoff — Arm64 Boot 的 C 入口（由 Boot.S 调用）
 *
 * @param DtbPhys  KernelEntry 从 x0 传来的 DTB；可能为 0 或无效
 *
 * 步骤：
 *   1) 清零 Info；
 *   2) 解析 DTB 得 RAM；失败则试固定 DTB 址；再失败用 virt 回退常量；
 *   3) 登记 KernelStart/End、DtbPhys；
 *   4) 把 RAM 切成「内核占用(不可分配)」+「其后空地(可分配)」；
 *   5) KernelMain(&Info)；若返回则死循环。
 */
void KernelHandoff(UINT64 DtbPhys) {
    static BOOT_INFO Info; /* BSS：依赖 Boot.S 清零；static 避免吃光临时栈 */
    UINT64 KernelStart = ARM64_VIRT_RAM_BASE;
    UINT64 KernelEnd = (UINT64)(UINTN)__kernel_end;
    UINT64 RamBase = ARM64_VIRT_RAM_BASE;
    UINT64 RamSize = ARM64_VIRT_RAM_SIZE;
    UINT64 FreeStart;
    UINT64 UsedDtb = 0;
    int FromDtb;

    MemZero(&Info, sizeof(Info));

    /* 优先调用方给的 DTB；不行再试 virt 固定落点 */
    FromDtb = (DtbMemoryRegion(DtbPhys, &RamBase, &RamSize) == 0);
    if (FromDtb) {
        UsedDtb = DtbPhys;
    } else if (DtbMemoryRegion(ARM64_VIRT_DTB_ADDR, &RamBase, &RamSize) == 0) {
        FromDtb = 1;
        UsedDtb = ARM64_VIRT_DTB_ADDR;
    }
    if (!FromDtb) {
        RamBase = ARM64_VIRT_RAM_BASE;
        RamSize = ARM64_VIRT_RAM_SIZE;
    }

    Info.KernelStart = KernelStart;
    Info.KernelEnd = KernelEnd;
    Info.DtbPhys = UsedDtb; /* 解析失败则为 0：Kernel 勿解引用 */

    /*
     * 页对齐后的「内核末尾」：之前标保留，之后标可分配。
     * virt 上内核通常从 RAM 基址加载，故 [RamBase, FreeStart) 含内核映像。
     */
    FreeStart = (KernelEnd + 0xFFFULL) & ~0xFFFULL;
    if (FreeStart < KernelStart) {
        FreeStart = KernelStart;
    }
    if (FreeStart > RamBase && FreeStart - RamBase <= RamSize) {
        BootInfoAddRegion(&Info, RamBase, FreeStart - RamBase, 0); /* 保留 */
        if (FreeStart < RamBase + RamSize) {
            BootInfoAddRegion(&Info, FreeStart, RamBase + RamSize - FreeStart, 1); /* 可分配 */
        }
    } else if (RamSize > 0) {
        /* 切段条件不满足时：整段 RAM 先标可分配，交给后续 PMM 再收紧 */
        BootInfoAddRegion(&Info, RamBase, RamSize, 1);
    }

    HalSerialInitialize();
    HalSerialWrite("handoff: BOOT_INFO ready\n");
    /* 汇合点：此后与 X64（经 HAL 转 BOOT_INFO）走同一套 KernelMain */
    KernelMain(&Info);
    for (;;) {
    }
}

#endif
