/*
 * setjmp_aarch64.S — setjmp / longjmp（AAPCS64）
 * jmp_buf: x19 x20 x21 x22 x23 x24 x25 x26 x27 x28 x29(fp) x30(lr) sp pad
 */
	.text
	.global setjmp
	.type setjmp, %function
setjmp:
	stp	x19, x20, [x0, #0]
	stp	x21, x22, [x0, #16]
	stp	x23, x24, [x0, #32]
	stp	x25, x26, [x0, #48]
	stp	x27, x28, [x0, #64]
	stp	x29, x30, [x0, #80]
	mov	x1, sp
	str	x1, [x0, #96]
	mov	w0, #0
	ret
	.size setjmp, .-setjmp

	.global longjmp
	.type longjmp, %function
longjmp:
	mov	w2, w1
	cmp	w2, #0
	csinc	w2, w2, wzr, ne
	ldp	x19, x20, [x0, #0]
	ldp	x21, x22, [x0, #16]
	ldp	x23, x24, [x0, #32]
	ldp	x25, x26, [x0, #48]
	ldp	x27, x28, [x0, #64]
	ldp	x29, x30, [x0, #80]
	ldr	x1, [x0, #96]
	mov	sp, x1
	mov	w0, w2
	ret
	.size longjmp, .-longjmp
