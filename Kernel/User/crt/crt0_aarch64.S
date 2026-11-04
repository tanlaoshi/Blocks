/* User/crt/crt0_aarch64.S — _start → BlocksCrtCallMain → exit(SVC)
 * 号段重排后 SYS_PROCESS_EXIT=50（勿再写死 0）
 */
#include <Blocks/syscall.h>
	.section .text
	.global _start
	.extern BlocksCrtCallMain
_start:
	ldr	x0, [sp]
	add	x1, sp, #8
	bl	BlocksCrtCallMain
	mov	x8, #SYS_PROCESS_EXIT
	mov	x1, xzr
	mov	x2, xzr
	svc	#0
1:	b	1b
