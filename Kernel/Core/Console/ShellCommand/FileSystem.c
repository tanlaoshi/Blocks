/*
 * FileSystem.c — Shell 侧 FAT 根目录命令（ls/vols/cat/write/mkdir/rm/mv）
 *
 * 【初学者】
 * - 分层：Core/Console/ShellCommand；薄包装，逻辑在 FileSystem/Fat* 与 Volume
 * - 对外入口：FileSystemRegister（挂命令表）
 * - 不做：子目录 ls、跨卷 mv（Fat* 层同样限制）
 */
#include "ShellCommand.h"
#include "FatFile.h"
#include "HalSerial.h"
#include "Volume.h"

static void Put(const char *S) {
    HalSerialWriteShell(S);
}

static void PutHex32(UINT32 V) {
    char Hex[12];
    HalSerialFormatHex(Hex, V, 8);
    Put(Hex);
}

static void CommandLs(int Argc, char **Argv) {
    FAT_DIR_ENT Ents[64];
    UINT32 N = 0;
    UINT32 i;
    const char *Rel = "";

    if (Argc >= 2) {
        if (VolumeResolve(Argv[1], &Rel) != 0) {
            Put("ls: bad vol\n");
            return;
        }
        /* 有卷前缀后的子路径：本刀只列根 */
        if (Rel != 0 && Rel[0] != 0) {
            Put("ls: use VOL or VOL: (subdir later)\n");
            return;
        }
    } else if (VolumeResolve("", &Rel) != 0) {
        Put("ls: fail\n");
        return;
    }
    if (FatDirectoryListRoot(Ents, 64, &N) != 0) {
        Put("ls: fail\n");
        return;
    }
    for (i = 0; i < N; i++) {
        Put(Ents[i].IsDir ? "d " : "- ");
        PutHex32(Ents[i].Size);
        Put("  ");
        Put(Ents[i].Name);
        Put("\n");
    }
}

/* vols — 列出挂载卷 */
static void CommandVols(int Argc, char **Argv) {
    int N;
    int i;
    (void)Argc;
    (void)Argv;

    if (VolumeCount() <= 0 && VolumeMountAll() != 0) {
        Put("vols: none\n");
        return;
    }
    N = VolumeCount();
    for (i = 0; i < N; i++) {
        const VOLUME *V = VolumeGet(i);
        char Let[3];
        if (V == 0) {
            continue;
        }
        Let[0] = V->Letter;
        Let[1] = ':';
        Let[2] = 0;
        Put(Let);
        Put(" ");
        Put(V->Name);
        Put(V->ReadOnly ? " ro" : " rw");
        Put(V->IsEsp ? " esp" : " fat");
        if (i == VolumeDefaultIndex()) {
            Put(" *");
        }
        Put("\n");
    }
}

static void CommandCat(int Argc, char **Argv) {
    UINT8 Buf[2048];
    UINT32 Size = 0;
    int N;
    UINT32 i;
    if (Argc < 2) {
        Put("cat: need file\n");
        return;
    }
    N = FatFileReadPath(Argv[1], Buf, sizeof(Buf) - 1u, &Size);
    if (N < 0) {
        Put("cat: fail\n");
        return;
    }
    /* 按目录 Size 输出；勿遇 0 提前停（短 Size/垫零会导致「只见首行」） */
    for (i = 0; i < Size; i++) {
        char One[2];
        UINT8 C = Buf[i];
        if (C == '\n' || C == '\r' || (C >= 0x20u && C <= 0x7eu)) {
            One[0] = (char)C;
            One[1] = 0;
            Put(One);
        } else if (C == 0) {
            Put(".");
        } else {
            Put(".");
        }
    }
    if (Size == 0 || Buf[Size - 1] != '\n') {
        Put("\n");
    }
}


static void CommandWrite(int Argc, char **Argv) {
    char Body[512];
    int o = 0;
    int i, j;
    if (Argc < 2) {
        Put("write: need name [text]\n");
        return;
    }
    for (i = 2; i < Argc; i++) {
        if (i > 2 && o + 1 < (int)sizeof(Body)) {
            Body[o++] = ' ';
        }
        for (j = 0; Argv[i][j] && o + 1 < (int)sizeof(Body); j++) {
            Body[o++] = Argv[i][j];
        }
    }
    Body[o] = 0;
    if (FatFileWritePath(Argv[1], Body, (UINT32)o) != 0) {
        Put("write: fail\n");
        return;
    }
    Put("write: ok\n");
}

static void CommandMakeDirectory(int Argc, char **Argv) {
    if (Argc < 2) {
        Put("mkdir: need name\n");
        return;
    }
    if (FatMakeDirectory(Argv[1]) != 0) {
        Put("mkdir: fail\n");
        return;
    }
    Put("mkdir: ok\n");
}

static void CommandRemove(int Argc, char **Argv) {
    if (Argc < 2) {
        Put("rm: need name\n");
        return;
    }
    if (FatDeleteFile(Argv[1]) != 0) {
        Put("rm: fail\n");
        return;
    }
    Put("rm: ok\n");
}

/* mv — 同卷改名 */
static void CommandMv(int Argc, char **Argv) {
    if (Argc < 3) {
        Put("usage: mv <old> <new>\n");
        return;
    }
    if (FatRenamePath(Argv[1], Argv[2]) != 0) {
        Put("mv: fail\n");
        return;
    }
    Put("mv: ok\n");
}

/*
 * FileSystemRegister — 注册 ls/vols/cat/write/mkdir/rm/mv
 *
 * 做什么：把本文件各 Command* 挂进全局 Shell 命令表。
 * 谁调用：ShellCommandInitialize（Console 初始化链）。
 * 前后文：
 *   前 — ShellCommandRegister 表已清零
 *   后 — ThemeRegister / NetworkRegister 等同批 Register
 * 返回：void（失败仅当 ShellCommandRegister 满表，静默丢命令）
 */
void FileSystemRegister(void) {
    ShellCommandRegister("ls", "list root dir [VOL:]", CommandLs);
    ShellCommandRegister("vols", "list mounted volumes", CommandVols);
    ShellCommandRegister("cat", "print text file", CommandCat);
    ShellCommandRegister("write", "write text file", CommandWrite);
    ShellCommandRegister("mkdir", "make directory", CommandMakeDirectory);
    ShellCommandRegister("rm", "remove file/dir", CommandRemove);
    ShellCommandRegister("mv", "rename file/dir", CommandMv);
}
