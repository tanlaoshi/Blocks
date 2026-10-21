/*
 * Nav.c — Files 路径/卷侧栏/预览
 *
 * 侧栏按 FileSystem 已挂载卷动态生成；BLOCKS 置顶。
 * SecondaryReady=0：只展 OsDisk（ROOT.CFG 启动卷所在盘）。
 */
#include "FilesUiPrivate.h"
#include "Fat.h"

static int NameIsBlocks(const char *Name) {
    return Name && (Name[0] == 'B' || Name[0] == 'b') &&
           (Name[1] == 'L' || Name[1] == 'l') &&
           (Name[2] == 'O' || Name[2] == 'o') &&
           (Name[3] == 'C' || Name[3] == 'c') &&
           (Name[4] == 'K' || Name[4] == 'k') &&
           (Name[5] == 'S' || Name[5] == 's') && Name[6] == 0;
}

static int NameIsRes(const char *Name) {
    return Name && (Name[0] == 'R' || Name[0] == 'r') &&
           (Name[1] == 'E' || Name[1] == 'e') &&
           (Name[2] == 'S' || Name[2] == 's') && Name[3] == 0;
}

void RebuildPlaces(void) {
    int N;
    int i;
    int j;
    int PlaceN = 0;
    char Names[FS_MAX_VOLUMES][FS_VOL_NAME_MAX];
    char Letters[FS_MAX_VOLUMES];
    int Order[FS_MAX_VOLUMES];
    int VolN = 0;

    gPlaceCount = 0;
    N = FileSystemVolCount();
    if (N > FS_MAX_VOLUMES) {
        N = FS_MAX_VOLUMES;
    }

    for (i = 0; i < N; i++) {
        UINT32 Drive = 0;
        int Ro = 0;

        if (!FileSystemSecondaryVolsReady() && !FileSystemVolOnOsDisk(i)) {
            continue;
        }
        Names[VolN][0] = 0;
        if (FileSystemVolInfo(i, Names[VolN], FS_VOL_NAME_MAX, &Drive, 0, &Ro) != 0) {
            continue;
        }
        Letters[VolN] = (char)('A' + i);
        Order[VolN] = VolN;
        VolN++;
    }

    /* 排序：BLOCKS 置顶，RES 垫底，其余保持挂载序 */
    for (i = 0; i < VolN; i++) {
        for (j = i + 1; j < VolN; j++) {
            int Ai = Order[i];
            int Aj = Order[j];
            int Swap = 0;

            if (NameIsBlocks(Names[Aj]) && !NameIsBlocks(Names[Ai])) {
                Swap = 1;
            } else if (!NameIsBlocks(Names[Ai]) && !NameIsBlocks(Names[Aj])) {
                if (NameIsRes(Names[Ai]) && !NameIsRes(Names[Aj])) {
                    Swap = 1;
                }
            }
            if (Swap) {
                int T = Order[i];
                Order[i] = Order[j];
                Order[j] = T;
            }
        }
    }

    for (i = 0; i < VolN && PlaceN < FILES_PLACE_MAX; i++) {
        int V = Order[i];
        int k;
        int NameUnique = 1;

        for (j = 0; j < VolN; j++) {
            if (j != V && FilesUiStrEqIgnoreCase(Names[V], Names[j])) {
                NameUnique = 0;
                break;
            }
        }
        /*
         * 标签可同名（两份 BLOCKS:）；撞名时路径用字母 A:（Resolve 唯一）。
         * 区分靠 Preview 里的 DiskGUID，侧栏不加 dN。
         */
        if (NameUnique && Names[V][0]) {
            for (k = 0; Names[V][k] && k < FILES_PLACE_PATH_MAX - 2; k++) {
                gPlacePaths[PlaceN][k] = Names[V][k];
            }
            gPlacePaths[PlaceN][k++] = ':';
            gPlacePaths[PlaceN][k] = 0;
        } else {
            gPlacePaths[PlaceN][0] = Letters[V];
            gPlacePaths[PlaceN][1] = ':';
            gPlacePaths[PlaceN][2] = 0;
        }

        if (Names[V][0]) {
            for (k = 0; Names[V][k] && k < FILES_PLACE_LABEL_MAX - 2; k++) {
                gPlaceLabels[PlaceN][k] = Names[V][k];
            }
            gPlaceLabels[PlaceN][k++] = ':';
            gPlaceLabels[PlaceN][k] = 0;
        } else {
            for (k = 0; gPlacePaths[PlaceN][k] && k < FILES_PLACE_LABEL_MAX - 1; k++) {
                gPlaceLabels[PlaceN][k] = gPlacePaths[PlaceN][k];
            }
            gPlaceLabels[PlaceN][k] = 0;
        }

        gPlaces[PlaceN].Label = gPlaceLabels[PlaceN];
        gPlaces[PlaceN].Path = gPlacePaths[PlaceN];
        PlaceN++;
    }

    gPlaceCount = PlaceN;
}

/* cwd 是否落在该侧栏项（卷根或卷内子路径） */
int PlaceMatches(int Idx) {
    const char *P;
    int n;
    int i;

    if (Idx < 0 || Idx >= gPlaceCount) {
        return 0;
    }
    P = gPlaces[Idx].Path;
    if (!P || !P[0]) {
        return 0;
    }
    if (PathEqIgnoreCase(gCwd, P)) {
        return 1;
    }

    n = 0;
    while (P[n]) {
        n++;
    }

    /* 空 cwd：仅默认卷的卷根项命中 */
    if (gCwd[0] == 0 && n > 0 && P[n - 1] == ':') {
        char DefName[FS_VOL_NAME_MAX];
        char DefPath[FS_VOL_NAME_MAX + 2];
        int Def = FileSystemDefaultVol();
        int L = 0;

        if (Def >= 0 &&
            FileSystemVolInfo(Def, DefName, (int)sizeof(DefName), 0, 0, 0) == 0 &&
            DefName[0]) {
            while (DefName[L] && L < FS_VOL_NAME_MAX - 1) {
                DefPath[L] = DefName[L];
                L++;
            }
            DefPath[L++] = ':';
            DefPath[L] = 0;
            if (PathEqIgnoreCase(P, DefPath)) {
                return 1;
            }
        }
        return 0;
    }

    /* 前缀匹配（大小写不敏感） */
    for (i = 0; P[i] && gCwd[i]; i++) {
        char Ca = P[i];
        char Cb = gCwd[i];
        if (Ca >= 'A' && Ca <= 'Z') {
            Ca = (char)(Ca - 'A' + 'a');
        }
        if (Cb >= 'A' && Cb <= 'Z') {
            Cb = (char)(Cb - 'A' + 'a');
        }
        if (Ca != Cb) {
            return 0;
        }
    }
    if (P[i] != 0) {
        return 0;
    }
    /* Path 耗尽：卷根 BLOCKS: 匹配 BLOCKS:Apps；目录捷径需边界 / 或结束 */
    if (n > 0 && P[n - 1] == ':') {
        return 1;
    }
    if (gCwd[i] == 0 || gCwd[i] == '/') {
        return 1;
    }
    return 0;
}

void SyncSideSel(void) {
    int i;
    gSideSel = -1;
    for (i = 0; i < gPlaceCount; i++) {
        if (PlaceMatches(i)) {
            gSideSel = i;
            break;
        }
    }
}
