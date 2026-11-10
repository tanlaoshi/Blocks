/*
 * ServeClient.c — Accept 后读命令行并跑 info/selftest/Transfer
 */
#include "Ksend/Private.h"

#define KSEND_SERVE_LINE_BYTES 128u

static int ArgumentIs(const char *Argument, const char *Word) {
    if (Argument == 0 || Word == 0) {
        return 0;
    }
    while (*Argument != 0 && *Word != 0) {
        char Left = *Argument;
        char Right = *Word;

        if (Left >= 'A' && Left <= 'Z') {
            Left = (char)(Left - 'A' + 'a');
        }
        if (Right >= 'A' && Right <= 'Z') {
            Right = (char)(Right - 'A' + 'a');
        }
        if (Left != Right) {
            return 0;
        }
        Argument++;
        Word++;
    }
    return *Argument == 0 && *Word == 0;
}

static int ReadCommandLine(char *Line, int Capacity) {
    int Length = 0;
    UINT8 Byte;

    if (Line == 0 || Capacity < 2) {
        return -1;
    }
    for (;;) {
        if (KsendTransportGetByte(&Byte, 30000u) == 0) {
            return -1;
        }
        if (Byte == (UINT8)'\n') {
            break;
        }
        if (Byte == (UINT8)'\r') {
            continue;
        }
        if (Length + 1 < Capacity) {
            Line[Length++] = (char)Byte;
        }
    }
    Line[Length] = 0;
    return Length;
}

/*
 * 0=transfer；1=info；2=selftest；-1=坏行。
 * Reboot/Backup 入参为板默认；仅主机 flag 覆盖。
 */
static int ParseServeCommand(char *Line, const char **TargetPath, int *Reboot,
                             int *Backup) {
    char *Cursor;
    char *Token;
    int SawKsend = 0;
    int Mode = 0;

    *TargetPath = 0;
    Cursor = Line;
    while (*Cursor != 0) {
        while (*Cursor == ' ' || *Cursor == '\t') {
            Cursor++;
        }
        if (*Cursor == 0) {
            break;
        }
        Token = Cursor;
        while (*Cursor != 0 && *Cursor != ' ' && *Cursor != '\t') {
            Cursor++;
        }
        if (*Cursor != 0) {
            *Cursor = 0;
            Cursor++;
        }
        if (!SawKsend) {
            if (!ArgumentIs(Token, "ksend") && !ArgumentIs(Token, "hotsend")) {
                return -1;
            }
            SawKsend = 1;
            continue;
        }
        if (ArgumentIs(Token, "--no-reboot")) {
            *Reboot = 0;
        } else if (ArgumentIs(Token, "--no-backup")) {
            *Backup = 0;
        } else if (ArgumentIs(Token, "info")) {
            Mode = 1;
        } else if (ArgumentIs(Token, "selftest")) {
            Mode = 2;
        } else if (ArgumentIs(Token, "serve")) {
            return -1;
        } else {
            *TargetPath = Token;
        }
    }
    if (!SawKsend) {
        return -1;
    }
    return Mode;
}

int KsendServeRunClient(int ClientSocket) {
    char Line[KSEND_SERVE_LINE_BYTES];
    const char *TargetPath;
    int Reboot;
    int Backup;
    int Result;

    KsendTransportUseSocket(ClientSocket);
    Result = ReadCommandLine(Line, (int)sizeof(Line));
    if (Result < 0) {
        KsendNote("serve: command timeout");
        return 1;
    }
    Reboot = KsendServePreferenceReboot() ? 1 : 0;
    Backup = KsendServePreferenceBackup() ? 1 : 0;
    Result = ParseServeCommand(Line, &TargetPath, &Reboot, &Backup);
    if (Result < 0) {
        KsendNote("serve: bad command line");
        return 1;
    }
    if (Result == 1) {
        return KsendInfo();
    }
    if (Result == 2) {
        return KsendSelfTest();
    }
    return KsendTransfer(TargetPath, Reboot, Backup);
}
