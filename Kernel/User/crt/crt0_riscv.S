/* User/crt/crt0_riscv.S — PR-A12：_start → main → exit(ecall)
 * 号段重排后 SYS_PROCESS_EXIT=50（勿再写死 0）
 */
#include <Blocks/syscall.h>
	.section .text
	.global _start
	.extern main
_start:
	ld	a0, 0(sp)
	addi	a1, sp, 8
	call	main
	/* a0 = main 返回值作退出码；a7 = SYS_PROCESS_EXIT */
	li	a7, SYS_PROCESS_EXIT
	ecall
1:	j	1b
