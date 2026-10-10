/*
 * NetworkUdp.c — udplisten / udpsend / udprecv
 *
 * 对标现网 ShellCommandsNetUdp.c。
 */
#include "ShellCommand.h"
#include "Network.h"
#include "HalSerial.h"

static void Put(const char *S) {
    HalSerialWriteShell(S);
}

static void PutUnsigned32(UINT32 V) {
    char Digits[12];
    int Count = 0;
    int Index;

    if (V == 0) {
        Put("0");
        return;
    }
    while (V != 0 && Count < 11) {
        Digits[Count++] = (char)('0' + (V % 10u));
        V /= 10u;
    }
    for (Index = Count - 1; Index >= 0; Index--) {
        char One[2];
        One[0] = Digits[Index];
        One[1] = 0;
        Put(One);
    }
}

static void PutIp(UINT32 Ip) {
    PutUnsigned32((Ip >> 24) & 0xFFu);
    Put(".");
    PutUnsigned32((Ip >> 16) & 0xFFu);
    Put(".");
    PutUnsigned32((Ip >> 8) & 0xFFu);
    Put(".");
    PutUnsigned32(Ip & 0xFFu);
}

static int ParsePort(const char *S, UINT32 *Out) {
    UINT32 V = 0;
    if (S == 0 || *S == 0) {
        return -1;
    }
    while (*S) {
        if (*S < '0' || *S > '9') {
            return -1;
        }
        V = V * 10u + (UINT32)(*S - '0');
        if (V > 65535u) {
            return -1;
        }
        S++;
    }
    if (V == 0) {
        return -1;
    }
    *Out = V;
    return 0;
}

static void DrainUdpOnce(void) {
    NETWORK_UDP_DG Dg;
    (void)UdpPoll(50);
    while (UdpRecv(&Dg)) {
        Put("udp: recv from ");
        PutIp(Dg.SrcIp);
        Put(":");
        PutUnsigned32(Dg.SrcPort);
        Put(" len=");
        PutUnsigned32(Dg.Len);
        Put(" ");
        {
            char Tmp[64];
            UINTN i;
            UINTN N = Dg.Len;
            if (N > sizeof(Tmp) - 1u) {
                N = sizeof(Tmp) - 1u;
            }
            for (i = 0; i < N; i++) {
                Tmp[i] = (char)Dg.Data[i];
            }
            Tmp[N] = 0;
            Put(Tmp);
        }
        Put("\n");
    }
}

static void CommandUdpListen(int Argc, char **Argv) {
    UINT32 Port = 0;

    if (Argc < 2 || ParsePort(Argv[1], &Port) != 0) {
        Put("usage: udplisten <port>\n");
        return;
    }
    if (!NetworkNicReady()) {
        Put("udp: no nic\n");
        return;
    }
    UdpBind((UINT16)Port);
    Put("udp: listening ");
    PutUnsigned32(Port);
    Put(" (then udpsend / udprecv)\n");
}

/* udprecv — 短轮询并打印队列 */
static void CommandUdpRecv(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    if (!NetworkNicReady()) {
        Put("udp: no nic\n");
        return;
    }
    DrainUdpOnce();
}

/* udpsend <ip> <port> <text>；发本机 IP 走回环，可立刻 udprecv */
static void CommandUdpSend(int Argc, char **Argv) {
    UINT32 Ip;
    UINT32 Port = 0;
    UINTN Len = 0;

    if (Argc < 4) {
        Put("usage: udpsend <ip> <port> <text>\n");
        return;
    }
    if (!NetworkNicReady()) {
        Put("udp: no nic\n");
        return;
    }
    if (NetworkParseIp(Argv[1], &Ip) != 0) {
        Put("udp: bad ip\n");
        return;
    }
    if (ParsePort(Argv[2], &Port) != 0) {
        Put("udp: bad port\n");
        return;
    }
    while (Argv[3][Len]) {
        Len++;
    }
    if (UdpSend(Ip, (UINT16)Port, Argv[3], Len) != 0) {
        Put("udp: send fail\n");
        return;
    }
    Put("udp: sent\n");
    if (Ip == NetworkSelfIp()) {
        DrainUdpOnce();
    }
}

void NetworkUdpRegister(void) {
    ShellCommandRegister("udplisten", "bind UDP port", CommandUdpListen);
    ShellCommandRegister("udpsend", "send UDP text", CommandUdpSend);
    ShellCommandRegister("udprecv", "poll/print UDP queue", CommandUdpRecv);
}
