/*
 * setjmp_riscv.S — setjmp / longjmp（RV64 ABI）
 * jmp_buf: s0–s11 ra sp pad…
 */
	.text
	.globl setjmp
	.type setjmp, @function
setjmp:
	sd	s0, 0(a0)
	sd	s1, 8(a0)
	sd	s2, 16(a0)
	sd	s3, 24(a0)
	sd	s4, 32(a0)
	sd	s5, 40(a0)
	sd	s6, 48(a0)
	sd	s7, 56(a0)
	sd	s8, 64(a0)
	sd	s9, 72(a0)
	sd	s10, 80(a0)
	sd	s11, 88(a0)
	sd	ra, 96(a0)
	sd	sp, 104(a0)
	li	a0, 0
	ret
	.size setjmp, .-setjmp

	.globl longjmp
	.type longjmp, @function
longjmp:
	mv	a2, a1
	bnez	a2, 1f
	li	a2, 1
1:
	ld	s0, 0(a0)
	ld	s1, 8(a0)
	ld	s2, 16(a0)
	ld	s3, 24(a0)
	ld	s4, 32(a0)
	ld	s5, 40(a0)
	ld	s6, 48(a0)
	ld	s7, 56(a0)
	ld	s8, 64(a0)
	ld	s9, 72(a0)
	ld	s10, 80(a0)
	ld	s11, 88(a0)
	ld	ra, 96(a0)
	ld	sp, 104(a0)
	mv	a0, a2
	ret
	.size longjmp, .-longjmp
