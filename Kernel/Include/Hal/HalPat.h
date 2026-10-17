/*
 * HalPat.h — 页属性表与 PTE 缓存位
 */
#ifndef HAL_PAT_H
#define HAL_PAT_H

/* x86 PTE 缓存属性位；其它 arch Map 时可忽略 */
#define HAL_PAGE_PWT      (1ULL << 3)
#define HAL_PAGE_PCD      (1ULL << 4)
/* 4K 页 PAT 位（bit7）；2M 大页 PAT 在 bit12，勿与 PS 混淆 */
#define HAL_PAGE_PAT      (1ULL << 7)

/* PR-G-fb-wc：本核 IA32_PAT PA1=WC；非 x86 空实现 */
void HalPatApplyWc(void);

#endif
