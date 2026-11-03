/*
 * DtbCpuEnableMethod.c — 读 FDT 首个 cpu@* 的 enable-method（pi-smp 探针）
 */
#include "Dtb.h"
#include "DtbPrivate.h"
#include "CString.h"

static int NameIsCpuAt(const char *Name) {
    if (Name == 0 || Name[0] != 'c' || Name[1] != 'p' || Name[2] != 'u' ||
        Name[3] != '@') {
        return 0;
    }
    return 1;
}

/*
 * 成功 0：Out 写入 enable-method（截断到 Capacity-1）。
 * 无节点/无属性/坏 FDT：-1，Out 置空串（若 Capacity>0）。
 */
int DtbCpuEnableMethod(UINT64 DtbPhys, char *Out, int Capacity) {
    const UINT8 *Blob;
    const FDT_HEADER *Header;
    UINT32 Total;
    UINT32 StructOffset;
    UINT32 StructSize;
    UINT32 StringsOffset;
    UINT32 Offset;
    UINT32 End;
    int InsideCpu;

    if (Out != 0 && Capacity > 0) {
        Out[0] = 0;
    }
    if (Out == 0 || Capacity < 2 || DtbPhys == 0) {
        return -1;
    }
    Blob = (const UINT8 *)(UINTN)DtbPhys;
    Header = (const FDT_HEADER *)Blob;
    if (DtbBe32(&Header->Magic) != FDT_MAGIC) {
        return -1;
    }
    Total = DtbBe32(&Header->Totalsize);
    StructOffset = DtbBe32(&Header->OffDtStruct);
    StructSize = DtbBe32(&Header->SizeDtStruct);
    StringsOffset = DtbBe32(&Header->OffDtStrings);
    if (Total < sizeof(FDT_HEADER) || StructOffset >= Total ||
        StringsOffset >= Total || StructSize == 0 ||
        StructOffset + StructSize > Total) {
        return -1;
    }

    InsideCpu = 0;
    Offset = StructOffset;
    End = StructOffset + StructSize;
    while (Offset + 4 <= End) {
        UINT32 Token = DtbBe32(Blob + Offset);
        Offset += 4;
        if (Token == FDT_BEGIN_NODE) {
            const char *Name = (const char *)(Blob + Offset);
            UINT32 Length = 0;
            while (Offset + Length < End && Name[Length] != 0) {
                Length++;
            }
            Offset = DtbAlign4(Offset + Length + 1);
            InsideCpu = NameIsCpuAt(Name) ? 1 : 0;
            continue;
        }
        if (Token == FDT_END_NODE) {
            InsideCpu = 0;
            continue;
        }
        if (Token == FDT_NOP) {
            continue;
        }
        if (Token == FDT_END) {
            break;
        }
        if (Token != FDT_PROP) {
            return -1;
        }
        {
            UINT32 PropertyLength;
            UINT32 NameOffset;
            const char *PropertyName;
            const UINT8 *Value;
            int Copy;
            int Index;

            if (Offset + 8 > End) {
                return -1;
            }
            PropertyLength = DtbBe32(Blob + Offset);
            NameOffset = DtbBe32(Blob + Offset + 4);
            Offset += 8;
            if (StringsOffset + NameOffset >= Total ||
                Offset + PropertyLength > End) {
                return -1;
            }
            PropertyName = (const char *)(Blob + StringsOffset + NameOffset);
            Value = Blob + Offset;
            Offset = DtbAlign4(Offset + PropertyLength);

            if (InsideCpu == 0 || !StringEqual(PropertyName, "enable-method")) {
                continue;
            }
            if (PropertyLength == 0) {
                return -1;
            }
            Copy = (int)PropertyLength;
            if (Copy >= Capacity) {
                Copy = Capacity - 1;
            }
            for (Index = 0; Index < Copy; Index++) {
                char Character = (char)Value[Index];
                if (Character == 0) {
                    Copy = Index;
                    break;
                }
                Out[Index] = Character;
            }
            Out[Copy] = 0;
            return Out[0] != 0 ? 0 : -1;
        }
    }
    return -1;
}
