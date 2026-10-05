/* newlib's setjmp writes 448 bytes (AltiVec) into a 256-byte -mno-altivec jmp_buf and
truncates GPRs with stw. This one (ioQuake3-PS3) uses std/ld and writes 328 bytes. */

#include "ppc-asm.h"

FUNC_START(setjmp)
	/* align the buffer to 8 bytes */
	addi	3,3,7
	rldicr	3,3,0,59

	std	1,0(3)
	std	2,8(3)
	std	13,16(3)
	std	14,24(3)
	std	15,32(3)
	std	16,40(3)
	std	17,48(3)
	std	18,56(3)
	std	19,64(3)
	std	20,72(3)
	std	21,80(3)
	std	22,88(3)
	std	23,96(3)
	std	24,104(3)
	std	25,112(3)
	std	26,120(3)
	std	27,128(3)
	std	28,136(3)
	std	29,144(3)
	std	30,152(3)
	std	31,160(3)

	mflr	4
	std	4,168(3)
	mfcr	4
	std	4,176(3)

	stfd	14,184(3)
	stfd	15,192(3)
	stfd	16,200(3)
	stfd	17,208(3)
	stfd	18,216(3)
	stfd	19,224(3)
	stfd	20,232(3)
	stfd	21,240(3)
	stfd	22,248(3)
	stfd	23,256(3)
	stfd	24,264(3)
	stfd	25,272(3)
	stfd	26,280(3)
	stfd	27,288(3)
	stfd	28,296(3)
	stfd	29,304(3)
	stfd	30,312(3)
	stfd	31,320(3)

	li	3,0
	blr
FUNC_END(setjmp)


FUNC_START(longjmp)
	addi	3,3,7
	rldicr	3,3,0,59

	ld	1,0(3)
	ld	2,8(3)
	ld	13,16(3)
	ld	14,24(3)
	ld	15,32(3)
	ld	16,40(3)
	ld	17,48(3)
	ld	18,56(3)
	ld	19,64(3)
	ld	20,72(3)
	ld	21,80(3)
	ld	22,88(3)
	ld	23,96(3)
	ld	24,104(3)
	ld	25,112(3)
	ld	26,120(3)
	ld	27,128(3)
	ld	28,136(3)
	ld	29,144(3)
	ld	30,152(3)
	ld	31,160(3)

	ld	5,168(3)
	mtlr	5
	ld	5,176(3)
	mtcr	5

	lfd	14,184(3)
	lfd	15,192(3)
	lfd	16,200(3)
	lfd	17,208(3)
	lfd	18,216(3)
	lfd	19,224(3)
	lfd	20,232(3)
	lfd	21,240(3)
	lfd	22,248(3)
	lfd	23,256(3)
	lfd	24,264(3)
	lfd	25,272(3)
	lfd	26,280(3)
	lfd	27,288(3)
	lfd	28,296(3)
	lfd	29,304(3)
	lfd	30,312(3)
	lfd	31,320(3)

	/* setjmp's second return: the value, or 1 for 0 */
	mr.	3,4
	bnelr+
	li	3,1
	blr
FUNC_END(longjmp)
