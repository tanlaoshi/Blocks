/*
 * Gpt.h — K44：在当前 HalBlock 盘上找 FAT 起点（superfloppy / MBR / GPT）
 */
#ifndef GPT_H
#define GPT_H

#include "BootTypes.h"

#define GPT_FAT_MAX 4

typedef struct {
    UINT32 StartLba;
    int IsEsp; /* GPT EFI System 或 MBR 0xEF */
} GPT_FAT_PART;

/* 写入 Out[0..*OutCount)；成功 0；无 FAT 时 Count=0 仍 0 */
int GptFindFatParts(GPT_FAT_PART *Out, int Max, int *OutCount);

#endif
