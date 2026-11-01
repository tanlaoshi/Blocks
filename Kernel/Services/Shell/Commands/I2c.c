/*
 * I2c.c — shell 命令 i2c：总线扫描 + 寄存器读写（三架构共编）
 *
 * 子命令：
 *   i2c scan                     扫 0x03..0x77，打印有应答的地址（判据：能扫到器件）
 *   i2c read <addr> <reg> [n]    读寄存器（默认 1 字节）
 *   i2c write <addr> <reg> <b>   写 1 字节
 *
 * 为什么 scan 是这刀的核心判据：
 *   "扫到地址"来自器件自己的 ACK，不是我们写进去再读回来 —— 这是硬证据。
 *   已知地址参考：Grove LCD RGB 0x3E/0x62、Grove OLED 0x3C、
 *                 Sense HAT 传感器 0x5C/0x5F/0x6A/0x1C、ATTINY88 0x46。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "HalI2c.h"

#define I2C_ARG_COMMAND  1
#define I2C_ARG_FIRST    2
#define I2C_ARG_SECOND   3
#define I2C_ARG_THIRD    4
#define I2C_SCAN_FIRST   0x03u
#define I2C_SCAN_LAST    0x77u
#define I2C_TEXT_MAX     8u

static void I2cWriteUsage(void) {
    gConsole.Write("i2c scan | diag | read <addr> <reg> [n] | write <addr> <reg> <byte>\n");
}

static const char *I2cArg(int Argc, char **Argv, int Index) {
    return (Index < Argc) ? Argv[Index] : 0;
}

static int I2cParseByte(const char *Text, UINT8 *Out) {
    UINT32 Value = 0u;
    int Digits = 0;

    if (Text == 0 || Out == 0) {
        return -1;
    }
    if (Text[0] == '0' && (Text[1] == 'x' || Text[1] == 'X')) {
        Text += 2;
        while (*Text != 0) {
            UINT32 Digit;
            if (*Text >= '0' && *Text <= '9') {
                Digit = (UINT32)(*Text - '0');
            } else if (*Text >= 'a' && *Text <= 'f') {
                Digit = (UINT32)(*Text - 'a') + 10u;
            } else if (*Text >= 'A' && *Text <= 'F') {
                Digit = (UINT32)(*Text - 'A') + 10u;
            } else {
                return -1;
            }
            Value = (Value << 4) | Digit;
            Digits++;
            Text++;
        }
    } else {
        while (*Text >= '0' && *Text <= '9') {
            Value = Value * 10u + (UINT32)(*Text - '0');
            Digits++;
            Text++;
        }
    }
    if (Digits == 0 || *Text != 0 || Value > 0xFFu) {
        return -1;
    }
    *Out = (UINT8)Value;
    return 0;
}

static void I2cPrintByte(UINT8 Value) {
    static const char Digits[] = "0123456789abcdef";
    char Text[I2C_TEXT_MAX];

    Text[0] = '0';
    Text[1] = 'x';
    Text[2] = Digits[(Value >> 4) & 0xFu];
    Text[3] = Digits[Value & 0xFu];
    Text[4] = 0;
    gConsole.Write(Text);
}

static void I2cCommandDiag(void) {
    UINT32 Regs[5] = {0u, 0u, 0u, 0u, 0u};

    if (HalI2cDiagRead(Regs) != 0) {
        gConsole.Write("i2c: diag not supported on this board\n");
        return;
    }
    gConsole.Write("i2c: gpfsel0=");
    gConsole.WriteHex32(Regs[0]);
    gConsole.Write(" div=");
    gConsole.WriteHex32(Regs[1]);
    gConsole.Write(" c=");
    gConsole.WriteHex32(Regs[2]);
    gConsole.Write(" s=");
    gConsole.WriteHex32(Regs[3]);
    gConsole.Write(" probe=");
    gConsole.WriteHex32(Regs[4]);
    gConsole.Write("\n");
    {
        int Sda = -1;
        int Scl = -1;
        if (HalI2cLineLevels(&Sda, &Scl) == 0) {
            gConsole.Write("i2c: lines sda=");
            gConsole.WriteHex32((UINT32)Sda);
            gConsole.Write(" scl=");
            gConsole.WriteHex32((UINT32)Scl);
            gConsole.Write(" (both should be 1 with pull-ups)\n");
        }
    }
    gConsole.Write("i2c: hint gpfsel0 bits[8:6]=pin2 fsel, [11:9]=pin3 fsel (ALT0=4)\n");
}

static void I2cCommandScan(void) {
    UINT32 Address;
    UINT32 Found = 0u;

    if (HalI2cAvailable() == 0) {
        gConsole.Write("i2c: this board has no i2c (weak impl)\n");
        return;
    }
    gConsole.Write("i2c: scanning 0x03..0x77\n");
    for (Address = I2C_SCAN_FIRST; Address <= I2C_SCAN_LAST; Address++) {
        if (HalI2cProbe((UINT8)Address) == 0) {
            gConsole.Write("i2c: found ");
            I2cPrintByte((UINT8)Address);
            gConsole.Write("\n");
            Found++;
        }
    }
    gConsole.Write("i2c: scan done, devices=");
    gConsole.WriteHex32(Found);
    gConsole.Write(Found == 0u ? " (nothing ack'd: check SDA/SCL/GND/power)\n" : "\n");
}

static void I2cCommandRead(int Argc, char **Argv) {
    UINT8 Address;
    UINT8 Register;
    UINT8 Length = 1u;
    UINT8 Buffer[8];
    UINT32 Index;

    if (I2cParseByte(I2cArg(Argc, Argv, I2C_ARG_FIRST), &Address) != 0 ||
        I2cParseByte(I2cArg(Argc, Argv, I2C_ARG_SECOND), &Register) != 0) {
        I2cWriteUsage();
        return;
    }
    if (I2cArg(Argc, Argv, I2C_ARG_THIRD) != 0 &&
        (I2cParseByte(I2cArg(Argc, Argv, I2C_ARG_THIRD), &Length) != 0 || Length == 0u)) {
        I2cWriteUsage();
        return;
    }
    if (Length > (UINT8)sizeof(Buffer)) {
        Length = (UINT8)sizeof(Buffer);
    }
    if (HalI2cRead(Address, Register, Buffer, Length) != 0) {
        gConsole.Write("i2c: read FAILED (no ack or bus error)\n");
        return;
    }
    gConsole.Write("i2c: ");
    I2cPrintByte(Address);
    gConsole.Write("[");
    I2cPrintByte(Register);
    gConsole.Write("] =");
    for (Index = 0u; Index < Length; Index++) {
        gConsole.Write(" ");
        I2cPrintByte(Buffer[Index]);
    }
    gConsole.Write("\n");
}

static void I2cCommandWrite(int Argc, char **Argv) {
    UINT8 Address;
    UINT8 Register;
    UINT8 Value;

    if (I2cParseByte(I2cArg(Argc, Argv, I2C_ARG_FIRST), &Address) != 0 ||
        I2cParseByte(I2cArg(Argc, Argv, I2C_ARG_SECOND), &Register) != 0 ||
        I2cParseByte(I2cArg(Argc, Argv, I2C_ARG_THIRD), &Value) != 0) {
        I2cWriteUsage();
        return;
    }
    if (HalI2cWrite(Address, Register, &Value, 1u) != 0) {
        gConsole.Write("i2c: write FAILED\n");
        return;
    }
    gConsole.Write("i2c: write ok\n");
}

void CommandI2c(int Argc, char **Argv) {
    const char *Name = I2cArg(Argc, Argv, I2C_ARG_COMMAND);

    if (Name == 0) {
        I2cWriteUsage();
    } else if (Name[0] == 's' && Name[1] == 'c') {
        I2cCommandScan();
    } else if (Name[0] == 'd' && Name[1] == 'i') {
        I2cCommandDiag();
    } else if (Name[0] == 'r') {
        I2cCommandRead(Argc, Argv);
    } else if (Name[0] == 'w') {
        I2cCommandWrite(Argc, Argv);
    } else {
        I2cWriteUsage();
    }
}

void ShellCommandsI2cRegister(void) {
    gConsole.Register("i2c",
                      "i2c scan | read <addr> <reg> [n] | write <addr> <reg> <byte>",
                      CommandI2c);
}
