/*
 * SelectKernelImage.c — ksend-rollback：试跑计数超限则改载 Kernel.elf.bak
 *
 * boot.try 单字节计数（缺省 0）。每启 +1 写回；>MAX 且 .bak 可读则载 bak。
 * 真核 FS Ready 后清零（见 FileSystem 收口）。
 */
#include "BootTypes.h"
#include "Fat.h"

#define BOOT_STUB_ELF_BUFFER 0x84000000ULL
#define BOOT_STUB_ELF_BUFFER_MAX (12u * 1024u * 1024u)
#define BOOT_TRY_PATH "boot.try"
#define BOOT_KERNEL_PATH "Kernel.elf"
#define BOOT_KERNEL_BACKUP_PATH "Kernel.elf.bak"
#define BOOT_TRY_MAX_BEFORE_BACKUP 3u

extern void BootStubSerialWrite(const char *Text);
extern int BootStubFindBlocksVolumeStart(UINT32 *OutStartLogicalBlockAddress);

static void BootStubWriteUnsigned(UINT32 Value) {
    char Digits[12];
    int Count;
    int Index;
    char One[2];

    Count = 0;
    do {
        Digits[Count++] = (char)('0' + (Value % 10u));
        Value /= 10u;
    } while (Value != 0u && Count < (int)sizeof(Digits));
    for (Index = Count; Index > 0; Index--) {
        One[0] = Digits[Index - 1];
        One[1] = 0;
        BootStubSerialWrite(One);
    }
}

static UINT8 BootStubReadTryCount(void) {
    UINT8 Byte = 0;
    UINTN Got = 0;

    if (FatReadFile(BOOT_TRY_PATH, &Byte, 1, &Got) != FAT_OK || Got < 1u) {
        return 0;
    }
    return Byte;
}

static void BootStubWriteTryCount(UINT8 Count) {
    int Result;

    Result = FatWriteFile(BOOT_TRY_PATH, &Count, 1);
    if (Result != FAT_OK) {
        BootStubSerialWrite("stub: boot.try write fail\n");
        return;
    }
    (void)FatFileSync();
}

static int BootStubLoadNamedKernel(const char *Path, void **OutImage, UINTN *OutSize) {
    UINTN Got = 0;
    int Result;

    Result = FatReadFile(Path, (void *)(UINTN)BOOT_STUB_ELF_BUFFER,
                         BOOT_STUB_ELF_BUFFER_MAX, &Got);
    if (Result != FAT_OK || Got < 64u) {
        return -1;
    }
    *OutImage = (void *)(UINTN)BOOT_STUB_ELF_BUFFER;
    *OutSize = Got;
    return 0;
}

int BootStubReadKernelExecutable(void **OutImage, UINTN *OutSize) {
    UINT32 StartLogicalBlockAddress;
    UINT8 TryCount;
    FAT_FILE_STAT BackupStat;
    int UseBackup;

    if (OutImage == 0 || OutSize == 0) {
        return -1;
    }
    if (BootStubFindBlocksVolumeStart(&StartLogicalBlockAddress) != 0) {
        return -1;
    }
    if (FatInitialize(StartLogicalBlockAddress) != FAT_OK) {
        BootStubSerialWrite("stub: FatInitialize fail\n");
        return -1;
    }

    TryCount = BootStubReadTryCount();
    if (TryCount < 0xffu) {
        TryCount = (UINT8)(TryCount + 1u);
    }
    BootStubWriteTryCount(TryCount);

    UseBackup = 0;
    if (TryCount > BOOT_TRY_MAX_BEFORE_BACKUP &&
        FatFileStat(BOOT_KERNEL_BACKUP_PATH, &BackupStat) == FAT_OK &&
        BackupStat.Size >= 64u) {
        UseBackup = 1;
    }

    BootStubSerialWrite("stub: boot.try=");
    BootStubWriteUnsigned(TryCount);
    BootStubSerialWrite(UseBackup ? " load=Kernel.elf.bak\n" : " load=Kernel.elf\n");

    if (UseBackup) {
        if (BootStubLoadNamedKernel(BOOT_KERNEL_BACKUP_PATH, OutImage, OutSize) == 0) {
            return 0;
        }
        BootStubSerialWrite("stub: Kernel.elf.bak read fail; try Kernel.elf\n");
    }
    if (BootStubLoadNamedKernel(BOOT_KERNEL_PATH, OutImage, OutSize) == 0) {
        BootStubSerialWrite("stub: Kernel.elf loaded\n");
        return 0;
    }
    BootStubSerialWrite("stub: Kernel.elf read fail\n");
    /* 主核不可读时立刻试 bak（计数未超限也能自救；挂死仍靠 boot.try 落盘） */
    if (!UseBackup &&
        FatFileStat(BOOT_KERNEL_BACKUP_PATH, &BackupStat) == FAT_OK &&
        BackupStat.Size >= 64u &&
        BootStubLoadNamedKernel(BOOT_KERNEL_BACKUP_PATH, OutImage, OutSize) == 0) {
        BootStubSerialWrite("stub: fallback Kernel.elf.bak after read fail\n");
        return 0;
    }
    return -1;
}
