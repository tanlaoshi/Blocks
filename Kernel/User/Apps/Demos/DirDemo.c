/*
 * DirDemo.c — PR-F4：DirOpen / DirRead / FileStat 冒烟
 */
#include <stdio.h>
#include <dirent.h>
#include <string.h>

int main(void) {
    FS_DIR *Dir;
    FS_DIR_ENT Ent;
    FS_FILE_STAT St;
    int Count = 0;
    int SawHello = 0;
    int Rc;

    if (FileStat("HELLO.ELF", &St) != 0) {
        printf("dirdemo: FAIL FileStat HELLO.ELF\n");
        return 1;
    }
    if (St.Attr & FS_ATTR_DIR) {
        printf("dirdemo: FAIL HELLO.ELF marked dir\n");
        return 1;
    }
    if (St.Size == 0) {
        printf("dirdemo: FAIL HELLO.ELF size 0\n");
        return 1;
    }
    printf("filestat HELLO size=%u\n", St.Size);

    if (FileStat("", &St) != 0) {
        printf("dirdemo: FAIL FileStat root\n");
        return 1;
    }
    if ((St.Attr & FS_ATTR_DIR) == 0) {
        printf("dirdemo: FAIL root not dir\n");
        return 1;
    }

    Dir = DirOpen("");
    if (!Dir) {
        printf("dirdemo: FAIL DirOpen root\n");
        return 1;
    }
    while ((Rc = DirRead(Dir, &Ent)) > 0) {
        Count++;
        if (strcmp(Ent.Name, "HELLO.ELF") == 0) {
            SawHello = 1;
        }
        if (Count <= 4) {
            printf("ent %s%s\n", Ent.Name,
                   (Ent.Attr & FS_ATTR_DIR) ? "/" : "");
        }
    }
    if (Rc < 0) {
        printf("dirdemo: FAIL DirRead\n");
        DirClose(Dir);
        return 1;
    }
    DirClose(Dir);
    if (Count < 1 || !SawHello) {
        printf("dirdemo: FAIL root list count=%d hello=%d\n", Count, SawHello);
        return 1;
    }


    Dir = DirOpen("RES:");
    if (!Dir) {
        printf("dirdemo: FAIL DirOpen RES:\n");
        return 1;
    }
    Count = 0;
    while ((Rc = DirRead(Dir, &Ent)) > 0) {
        Count++;
    }
    DirClose(Dir);
    if (Rc < 0 || Count < 1) {
        printf("dirdemo: FAIL RES: list\n");
        return 1;
    }

    Dir = DirOpen("HELLO.ELF");
    if (Dir) {
        DirClose(Dir);
        printf("dirdemo: FAIL DirOpen on file\n");
        return 1;
    }

    printf("dirdemo: ok\n");
    return 0;
}
