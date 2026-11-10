/*
 * ModelRepo.c — Store 仓库路径读写（自 Model.c 纯搬 · PR-NORM-16）
 */
#include "StoreUiPrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "Hal.h"

void StoreUiFormatRepo(char *Out, int OutMax) {
    UINT32 Ip;
    UINT16 Port;
    char IpBuf[24];
    int i = 0;
    int j;
    UINT32 P;
    char Dig[8];
    int Dn;

    if (!Out || OutMax <= 0) {
        return;
    }
    StoreRepoGet(&Ip, &Port);
    HalNetFormatIp(Ip, IpBuf, (int)sizeof(IpBuf));
    Out[i++] = 'r';
    if (i < OutMax - 1) Out[i++] = 'e';
    if (i < OutMax - 1) Out[i++] = 'p';
    if (i < OutMax - 1) Out[i++] = 'o';
    if (i < OutMax - 1) Out[i++] = ':';
    if (i < OutMax - 1) Out[i++] = ' ';
    for (j = 0; IpBuf[j] && i < OutMax - 1; j++) {
        Out[i++] = IpBuf[j];
    }
    if (i < OutMax - 1) {
        Out[i++] = ':';
    }
    P = (UINT32)Port;
    Dn = 0;
    if (P == 0) {
        Dig[Dn++] = '0';
    } else {
        while (P > 0 && Dn < (int)sizeof(Dig)) {
            Dig[Dn++] = (char)('0' + (P % 10u));
            P /= 10u;
        }
    }
    while (Dn > 0 && i < OutMax - 1) {
        Out[i++] = Dig[--Dn];
    }
    Out[i] = 0;
}

void StoreUiApplyRepoFile(void) {
    UINT8 Buf[64];
    UINTN Size = 0;
    char Line[48];
    UINTN i = 0;
    UINTN j = 0;
    int Err;

    Err = FileSystemReadFile(STORE_REPO_PATH, Buf, sizeof(Buf) - 1u, &Size);
    if (Err != FAT_OK || Size == 0) {
        return;
    }
    Buf[Size] = 0;
    while (i < Size && (Buf[i] == ' ' || Buf[i] == '\t')) {
        i++;
    }
    while (i < Size && Buf[i] != '\n' && Buf[i] != '\r' && j + 1 < sizeof(Line)) {
        Line[j++] = (char)Buf[i++];
    }
    while (j > 0 && (Line[j - 1] == ' ' || Line[j - 1] == '\t')) {
        j--;
    }
    Line[j] = 0;
    if (Line[0] == 0 || Line[0] == '#') {
        return;
    }
    if (StoreRepoSet(Line) == 0) {
        StoreSetStatus("repo updated");
    }
}

int StoreUiWriteRepoFile(void) {
    UINT32 Ip;
    UINT16 Port;
    char IpBuf[24];
    char Line[40];
    int i = 0;
    int j;
    UINT32 P;
    char Dig[8];
    int Dn;
    int Err;

    Err = FileSystemMakeDirectory(STORE_DIR);
    if (Err != FAT_OK && Err != FAT_ERR_EXIST) {
        return Err;
    }
    StoreRepoGet(&Ip, &Port);
    HalNetFormatIp(Ip, IpBuf, (int)sizeof(IpBuf));
    for (j = 0; IpBuf[j] && i < (int)sizeof(Line) - 1; j++) {
        Line[i++] = IpBuf[j];
    }
    if (i < (int)sizeof(Line) - 1) {
        Line[i++] = ':';
    }
    P = (UINT32)Port;
    Dn = 0;
    if (P == 0) {
        Dig[Dn++] = '0';
    } else {
        while (P > 0 && Dn < (int)sizeof(Dig)) {
            Dig[Dn++] = (char)('0' + (P % 10u));
            P /= 10u;
        }
    }
    while (Dn > 0 && i < (int)sizeof(Line) - 1) {
        Line[i++] = Dig[--Dn];
    }
    Line[i++] = '\n';
    Err = FileSystemWriteFile(STORE_REPO_PATH, (const UINT8 *)Line, (UINTN)i);
    return Err == FAT_OK ? 0 : Err;
}
