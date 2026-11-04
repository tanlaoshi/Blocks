/* User/crt/crt0_riscv.S — _start → blocks_call_main → exit(ecall)
 * 号段重排后 SYS_PROCESS_EXIT=50（勿再写死 0）
 */
#include <Blocks/syscall.h>
	.section .text
	.global _start
	.extern blocks_call_main
_start:
	ld	a0, 0(sp)
	addi	a1, sp, 8
	call	blocks_call_main
	li	a7, SYS_PROCESS_EXIT
	ecall
1:	j	1b
