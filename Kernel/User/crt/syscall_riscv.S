/* User/crt/syscall_riscv.S — long blocks_syscall(n,a,b,c) via ecall */
	.section .text
	.global blocks_syscall
blocks_syscall:
	mv	a7, a0
	mv	a0, a1
	mv	a1, a2
	mv	a2, a3
	ecall
	ret

	.global blocks_syscall2
	.set blocks_syscall2, blocks_syscall
