/*
 * OsTargetRootConfig.c — 读 ESP 上 EFI/Blocks/ROOT.CFG（disk=/part= UUID）
 *
 * 自 MountOs / OsTargetResolve 纯搬。
 */
#include "FileSystemPrivate.h"
#include "VirtualFileSystem.h"

int ParseRootConfig(const char *Text, UINTN Length, UINT8 Disk[16],
                    UINT8 Part[16]) {
    UINTN Index = 0;
    int HaveDisk = 0;
    int HavePart = 0;

    while (Index < Length) {
        char Key[8];
        char Value[40];
        UINTN KeyLength = 0;
        UINTN ValueLength = 0;

        while (Index < Length &&
               (Text[Index] == '\n' || Text[Index] == '\r' || Text[Index] == ' ')) {
            Index++;
        }
        if (Index >= Length) {
            break;
        }
        if (Text[Index] == '#') {
            while (Index < Length && Text[Index] != '\n') {
                Index++;
            }
            continue;
        }
        KeyLength = 0;
        while (Index < Length && Text[Index] != '=' && Text[Index] != '\n' &&
               KeyLength < sizeof(Key) - 1) {
            Key[KeyLength++] = Text[Index++];
        }
        Key[KeyLength] = 0;
        if (Index >= Length || Text[Index] != '=') {
            while (Index < Length && Text[Index] != '\n') {
                Index++;
            }
            continue;
        }
        Index++;
        ValueLength = 0;
        while (Index < Length && Text[Index] != '\n' && Text[Index] != '\r' &&
               ValueLength < sizeof(Value) - 1) {
            Value[ValueLength++] = Text[Index++];
        }
        Value[ValueLength] = 0;
        if (Key[0] == 'd' && Key[1] == 'i' && Key[2] == 's' && Key[3] == 'k' &&
            Key[4] == 0) {
            if (GptParseUuidStr(Value, Disk)) {
                HaveDisk = 1;
            }
        } else if (Key[0] == 'p' && Key[1] == 'a' && Key[2] == 'r' &&
                   Key[3] == 't' && Key[4] == 0) {
            if (GptParseUuidStr(Value, Part)) {
                HavePart = 1;
            }
        }
    }
    return HaveDisk && HavePart;
}

int TryRootConfigFromVolume(int VolumeIndex, UINT8 Disk[16], UINT8 Part[16]) {
    char Buffer[256];
    UINTN Size = 0;

    if (FileSystemActivate(VolumeIndex) != FAT_OK) {
        return 0;
    }
    if (gVirtualFileSystem.ReadFile("EFI\\Blocks\\ROOT.CFG", Buffer,
                                    sizeof(Buffer) - 1, &Size) != FAT_OK) {
        Size = 0;
        if (gVirtualFileSystem.ReadFile("EFI/Blocks/ROOT.CFG", Buffer,
                                        sizeof(Buffer) - 1, &Size) != FAT_OK) {
            return 0;
        }
    }
    Buffer[Size] = 0;
    return ParseRootConfig(Buffer, Size, Disk, Part);
}
