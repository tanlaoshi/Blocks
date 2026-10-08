/*
 * BootTypes.h — 裸机共用的「整数别名」
 *
 * 【初学者】
 * 内核是 freestanding：不链接完整 C 标准库，也不能随便 #include <stdint.h>
 *（工具链/选项一变就可能踩坑）。所以 Boot 与 Kernel 约定一套固定宽度类型：
 *
 *   UINT8/16/32/64  — 无符号 8/16/32/64 位
 *   INT*            — 对应有符号
 *   UINTN           — 「本机指针一样宽」的无符号整数（这里按 64 位目标定为 UINT64）
 *
 * Boot 柱（EFI）和 Kernel 柱都可能 include 本头；布局必须两边一致，
 * 否则交接结构体（如 BOOT_INFO）会错位。
 *
 * 积木角色：胶水/ABI 底座，不是可替换政策。
 */
#ifndef BOOT_TYPES_H
#define BOOT_TYPES_H

#define NULL ((void *)0)

typedef unsigned long long  UINT64;
typedef unsigned int        UINT32;
typedef unsigned short      UINT16;
typedef unsigned char       UINT8;
typedef long long           INT64;
typedef int                 INT32;
typedef short               INT16;
typedef signed char         INT8;
typedef UINT64              UINTN;

#endif
