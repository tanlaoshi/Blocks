/*
 * DtbCpuReleaseAddress.c — 读第 N 个 cpu@* 的 cpu-release-addr（spin-table）
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

static UINT64 DtbBe64(const UINT8 *Value) {
    return ((UINT64)DtbBe32(Value) << 32) | (UINT64)DtbBe32(Value + 4);
}

int DtbCpuReleaseAddress(UINT64 DtbPhys, UINT32 CpuIndex, UINT64 *OutPhys) {
    const UINT8 *Blob;
    const FDT_HEADER *Header;
    UINT32 Total;
    UINT32 StructOffset;
    UINT32 StructSize;
    UINT32 StringsOffset;
    UINT32 Offset;
    UINT32 End;
    int InsideCpu;
    UINT32 CpuSeen;

    if (OutPhys == 0 || DtbPhys == 0) {
        return -1;
    }
    *OutPhys = 0;
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
    CpuSeen = 0;
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
            if (InsideCpu) {
                if (CpuSeen == CpuIndex) {
                    return -1; /* 本核无 release-addr */
                }
                CpuSeen++;
            }
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

            if (!InsideCpu || CpuSeen != CpuIndex) {
                continue;
            }
            if (!StringEqual(PropertyName, "cpu-release-addr")) {
                continue;
            }
            if (PropertyLength == 8u) {
                *OutPhys = DtbBe64(Value);
                return *OutPhys != 0 ? 0 : -1;
            }
            if (PropertyLength == 4u) {
                *OutPhys = (UINT64)DtbBe32(Value);
                return *OutPhys != 0 ? 0 : -1;
            }
            return -1;
        }
    }
    return -1;
}
