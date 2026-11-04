/* User/crt/syscall_aarch64.S — long blocks_syscall(n,a,b,c) via SVC */
	.section .text
	.global blocks_syscall
blocks_syscall:
	mov	x8, x0
	mov	x0, x1
	mov	x1, x2
	mov	x2, x3
	svc	#0
	ret

	.global blocks_syscall2
	.set blocks_syscall2, blocks_syscall
