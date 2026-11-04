/*
 * printf_format.c — Format / 整数与长度修饰（hosted-printf-length）
 */
#include <stdarg.h>
#include <stdint.h>
#include <Blocks/syscall.h>
#include "printf_private.h"

size_t PrintfFormatDouble(char *Dst, size_t Cap, double Value, int Spec, int Prec);

void PrintfOutChar(FmtOut *O, char C) {
    if (O->ToFd) {
        blocks_write(1, &C, 1);
        O->Pos++;
        return;
    }
    if (O->Cap == 0 || O->Pos + 1 < O->Cap) {
        if (O->Buf) {
            O->Buf[O->Pos] = C;
        }
    }
    O->Pos++;
}

void PrintfOutStr(FmtOut *O, const char *S) {
    if (!S) {
        S = "(null)";
    }
    while (*S) {
        PrintfOutChar(O, *S++);
    }
}

static void OutUInt(FmtOut *O, unsigned long V, unsigned Base, int Upper) {
    char Buf[32];
    int I = 0;
    const char *Dig = Upper ? "0123456789ABCDEF" : "0123456789abcdef";

    if (Base < 2 || Base > 16) {
        return;
    }
    if (V == 0) {
        PrintfOutChar(O, '0');
        return;
    }
    while (V > 0 && I < (int)sizeof(Buf)) {
        Buf[I++] = Dig[V % Base];
        V /= Base;
    }
    while (I > 0) {
        PrintfOutChar(O, Buf[--I]);
    }
}

static void OutInt(FmtOut *O, long V) {
    if (V < 0) {
        PrintfOutChar(O, '-');
        V = -V;
    }
    OutUInt(O, (unsigned long)V, 10, 0);
}

static void OutULongLong(FmtOut *O, unsigned long long V, unsigned Base, int Upper) {
    char Buf[64];
    int I = 0;
    const char *Dig = Upper ? "0123456789ABCDEF" : "0123456789abcdef";

    if (Base < 2 || Base > 16) {
        return;
    }
    if (V == 0) {
        PrintfOutChar(O, '0');
        return;
    }
    while (V > 0 && I < (int)sizeof(Buf)) {
        Buf[I++] = Dig[V % Base];
        V /= Base;
    }
    while (I > 0) {
        PrintfOutChar(O, Buf[--I]);
    }
}

static void OutLongLong(FmtOut *O, long long V) {
    if (V < 0) {
        PrintfOutChar(O, '-');
        V = -V;
    }
    OutULongLong(O, (unsigned long long)V, 10, 0);
}

static void OutUIntPad(
    FmtOut *O,
    unsigned long long V,
    unsigned Base,
    int Upper,
    int ZeroPad,
    int Width
) {
    char Tmp[64];
    int Ti = 0;
    const char *Dig = Upper ? "0123456789ABCDEF" : "0123456789abcdef";
    unsigned long long T = V;

    if (T == 0) {
        Tmp[Ti++] = '0';
    } else {
        while (T > 0 && Ti < (int)sizeof(Tmp)) {
            Tmp[Ti++] = Dig[T % Base];
            T /= Base;
        }
    }
    if (ZeroPad && Width > Ti) {
        int Z;
        for (Z = 0; Z < Width - Ti; Z++) {
            PrintfOutChar(O, '0');
        }
    }
    while (Ti > 0) {
        PrintfOutChar(O, Tmp[--Ti]);
    }
}

int PrintfFormat(FmtOut *O, const char *fmt, va_list ap) {
    const char *P;

    if (!fmt) {
        return 0;
    }
    for (P = fmt; *P; P++) {
        if (*P != '%') {
            PrintfOutChar(O, *P);
            continue;
        }
        P++;
        if (!*P) {
            break;
        }
        {
            int ZeroPad = 0;
            int Width = 0;
            int Prec = -1;
            int Len = 0;

            if (*P == '0') {
                ZeroPad = 1;
                P++;
            }
            while (*P >= '0' && *P <= '9') {
                Width = Width * 10 + (*P - '0');
                P++;
            }
            if (*P == '.') {
                Prec = 0;
                P++;
                while (*P >= '0' && *P <= '9') {
                    Prec = Prec * 10 + (*P - '0');
                    P++;
                }
            }
            if (*P == 'l') {
                P++;
                if (*P == 'l') {
                    Len = 2;
                    P++;
                } else {
                    Len = 1;
                }
            } else if (*P == 'z') {
                Len = 3;
                P++;
            }
            if (!*P) {
                break;
            }
            switch (*P) {
            case '%':
                PrintfOutChar(O, '%');
                break;
            case 'c':
                PrintfOutChar(O, (char)va_arg(ap, int));
                break;
            case 's':
                PrintfOutStr(O, va_arg(ap, const char *));
                break;
            case 'd':
            case 'i':
                if (Len == 2) {
                    OutLongLong(O, va_arg(ap, long long));
                } else if (Len == 1 || Len == 3) {
                    OutInt(O, va_arg(ap, long));
                } else {
                    OutInt(O, (long)va_arg(ap, int));
                }
                break;
            case 'u':
            case 'o': {
                unsigned long long V;
                unsigned Base = (*P == 'o') ? 8u : 10u;

                if (Len == 2) {
                    V = va_arg(ap, unsigned long long);
                } else if (Len == 1 || Len == 3) {
                    V = va_arg(ap, unsigned long);
                } else {
                    V = va_arg(ap, unsigned);
                }
                OutUIntPad(O, V, Base, 0, ZeroPad, Width);
                break;
            }
            case 'f':
            case 'F':
            case 'e':
            case 'E':
            case 'g':
            case 'G': {
                char Fbuf[96];
                double V = va_arg(ap, double);

                PrintfFormatDouble(Fbuf, sizeof(Fbuf), V, *P, Prec);
                PrintfOutStr(O, Fbuf);
                break;
            }
            case 'x':
            case 'X': {
                unsigned long long V;
                int Upper = (*P == 'X');

                if (Len == 2) {
                    V = va_arg(ap, unsigned long long);
                } else if (Len == 1 || Len == 3) {
                    V = va_arg(ap, unsigned long);
                } else {
                    V = va_arg(ap, unsigned);
                }
                OutUIntPad(O, V, 16, Upper, ZeroPad, Width);
                break;
            }
            case 'p': {
                void *Ptr = va_arg(ap, void *);

                PrintfOutStr(O, "0x");
                OutUIntPad(O, (unsigned long)(uintptr_t)Ptr, 16, 0, 0, 0);
                break;
            }
            default:
                PrintfOutChar(O, '%');
                PrintfOutChar(O, *P);
                break;
            }
        }
    }
    return (int)O->Pos;
}
