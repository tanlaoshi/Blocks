/*
 * ChatPeer.c — BLOCKS.DB chat.peer= 与 TCP 行发送
 */
#include "ChatPeer.h"

#include <stdio.h>
#include <string.h>
#include <Host/UnixStandard.h>
#include <Blocks/File.h>
#include <System/Socket.h>
#include <Net/Net.h>

static int ReadDecimal(const char **Cursor, unsigned *OutValue) {
    const char *Pointer = *Cursor;
    unsigned Value = 0;
    int HasDigit = 0;

    while (*Pointer >= '0' && *Pointer <= '9') {
        Value = Value * 10u + (unsigned)(*Pointer - '0');
        Pointer++;
        HasDigit = 1;
    }
    if (Value > 65535u || !HasDigit) {
        return -1;
    }
    *OutValue = Value;
    *Cursor = Pointer;
    return 0;
}

static int ParsePeerAddress(const char *Value, unsigned *OutIp,
                            unsigned short *OutPort) {
    const char *Pointer = Value;
    unsigned PartA, PartB, PartC, PartD, Port = CHAT_PORT_DEFAULT;

    if (Value == NULL || OutIp == NULL || OutPort == NULL) {
        return -1;
    }
    if (ReadDecimal(&Pointer, &PartA) != 0 || *Pointer++ != '.') {
        return -1;
    }
    if (ReadDecimal(&Pointer, &PartB) != 0 || *Pointer++ != '.') {
        return -1;
    }
    if (ReadDecimal(&Pointer, &PartC) != 0 || *Pointer++ != '.') {
        return -1;
    }
    if (ReadDecimal(&Pointer, &PartD) != 0) {
        return -1;
    }
    if (*Pointer == ':') {
        Pointer++;
        if (ReadDecimal(&Pointer, &Port) != 0) {
            return -1;
        }
    }
    while (*Pointer == ' ' || *Pointer == '\t') {
        Pointer++;
    }
    if (*Pointer != 0 || PartA > 255 || PartB > 255 || PartC > 255 || PartD > 255 ||
        Port == 0 || Port > 65535) {
        return -1;
    }
    *OutIp = NetIpv4(PartA, PartB, PartC, PartD);
    *OutPort = (unsigned short)Port;
    return 0;
}

/*
 * 整文件一次 FileRead 再按行扫。禁止逐字节 Read 写局部缓冲：
 * O2 曾把缓冲放寄存器跨 syscall → #PF。
 */
int ChatLoadPeer(unsigned *OutIp, unsigned short *OutPort) {
    unsigned Ip;
    unsigned short Port;
    int FileDescriptor;
    int Found = 0;
    char FileBuffer[512];
    ssize_t ByteCount;
    ssize_t Index;
    ssize_t LineStart;

    if (OutIp == NULL || OutPort == NULL) {
        return -1;
    }
    Ip = NetIpv4(10, 0, 2, 2);
    Port = CHAT_PORT_DEFAULT;

    FileDescriptor = FileOpen("BLOCKS.DB");
    if (FileDescriptor < 0) {
        FileDescriptor = FileOpen("/BLOCKS.DB");
    }
    if (FileDescriptor >= 0) {
        ByteCount = FileRead(FileDescriptor, FileBuffer, sizeof(FileBuffer) - 1);
        FileClose(FileDescriptor);
        if (ByteCount < 0) {
            ByteCount = 0;
        }
        FileBuffer[ByteCount] = 0;
        LineStart = 0;
        for (Index = 0; Index <= ByteCount; Index++) {
            char Character = (Index < ByteCount) ? FileBuffer[Index] : '\n';

            if (Character != '\n' && Character != '\r' && Index < ByteCount) {
                continue;
            }
            if (Index > LineStart) {
                char Line[128];
                ssize_t Length = Index - LineStart;
                char *Pointer;

                if (Length >= (ssize_t)sizeof(Line)) {
                    Length = (ssize_t)sizeof(Line) - 1;
                }
                memcpy(Line, FileBuffer + LineStart, (size_t)Length);
                Line[Length] = 0;
                Pointer = Line;
                while (*Pointer == ' ' || *Pointer == '\t') {
                    Pointer++;
                }
                if (strncmp(Pointer, "chat.peer=", 10) == 0) {
                    Pointer += 10;
                    while (*Pointer == ' ' || *Pointer == '\t') {
                        Pointer++;
                    }
                    if (ParsePeerAddress(Pointer, &Ip, &Port) != 0) {
                        printf("chat: bad chat.peer=%s — keep default\n", Pointer);
                        break;
                    }
                    Found = 1;
                    break;
                }
            }
            LineStart = Index + 1;
        }
    }

    *OutIp = Ip;
    *OutPort = Port;
    if (!Found) {
        printf("chat: no chat.peer; default 10.0.2.2:%u (QEMU); "
               "NUC set chat.peer=<host-lan>:9090\n",
               (unsigned)Port);
    }
    return 0;
}

int ChatSendLine(int SocketDescriptor, const char *Line, size_t Length) {
    char Outgoing[CHAT_LINE_MAXIMUM + 2];
    size_t Offset = 0;
    size_t Index;

    if (Length > CHAT_LINE_MAXIMUM) {
        return -1;
    }
    for (Index = 0; Index < Length; Index++) {
        Outgoing[Index] = Line[Index];
    }
    Outgoing[Length] = '\n';
    while (Offset < Length + 1) {
        ssize_t Written =
            send(SocketDescriptor, Outgoing + Offset, Length + 1 - Offset, 0);
        if (Written <= 0) {
            return -1;
        }
        Offset += (size_t)Written;
    }
    return 0;
}
