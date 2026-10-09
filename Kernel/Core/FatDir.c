/*
 * FatDir.c — K23：根目录列举
 */
#include "FatFile.h"
#include "FatVol.h"

typedef struct {
    FAT_DIR_ENT *Out;
    UINT32 Cap;
    UINT32 N;
} LIST_CTX;

static int OnList(const UINT8 *Ent, void *Ctx) {
    LIST_CTX *L = (LIST_CTX *)Ctx;
    if (L->N >= L->Cap) {
        return 1;
    }
    FatName83ToDisplay(Ent, L->Out[L->N].Name);
    L->Out[L->N].Size = FatRd32(Ent + 28);
    L->Out[L->N].IsDir = ((Ent[11] & 0x10u) != 0) ? 1 : 0;
    L->N++;
    return 0;
}

int FatDirListRoot(FAT_DIR_ENT *Out, UINT32 Cap, UINT32 *Count) {
    FAT_VOL V;
    LIST_CTX Ctx;

    if (Out == 0 || Cap == 0) {
        return -1;
    }
    if (FatVolOpen(&V) != 0) {
        return -1;
    }
    Ctx.Out = Out;
    Ctx.Cap = Cap;
    Ctx.N = 0;
    if (FatVolWalkRoot(&V, OnList, &Ctx) != 0) {
        return -1;
    }
    if (Count) {
        *Count = Ctx.N;
    }
    return 0;
}
