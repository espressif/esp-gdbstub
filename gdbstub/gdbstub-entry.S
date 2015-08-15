#include "xtensa-defs.h"

.global savedRegs
.global exceptionStack

	.text

.literal_position

/*
//Useful to uncomment this and recompile when you need to look up the opcodes that replace 
//the debug vector.
doNotIncludeThis:
	xsr	a2, DEBUG_EXCSAVE
	jx	a2
*/
	.text
	.global DebugExceptionEntry
	.align	4

/*
	uint32_t pc;
	uint32_t ps;
	uint32_t sar;
	uint32_t vpri;
	uint32_t a0;
	uint32_t a[14]; //a2..a15
	//No A1 because that is SP of the user stack.
*/

/*
We arrive here with all regs intact except for a2. The old contents of A2 are saved 
into the DEBUG_EXCSAVE special function register. EPC is the original PC.
*/
DebugExceptionEntry:

//Save all regs to structure
	movi	a2, savedRegs
	s32i	a0, a2, 0x10
	s32i	a1, a2, 0x04
//	rsr		a0, DEBUG_PS
//	s32i	a0, a2, 0x04
	rsr		a0, DEBUG_EXCSAVE //was R2
	s32i	a0, a2, 0x14
	s32i	a3, a2, 0x18
	s32i	a4, a2, 0x1c
	s32i	a5, a2, 0x20
	s32i	a6, a2, 0x24
	s32i	a7, a2, 0x28
	s32i	a8, a2, 0x2c
	s32i	a9, a2, 0x30
	s32i	a10, a2, 0x34
	s32i	a11, a2, 0x38
	s32i	a12, a2, 0x3c
	s32i	a13, a2, 0x40
	s32i	a14, a2, 0x44
	s32i	a15, a2, 0x48
	rsr		a0, SAR
	s32i	a0, a2, 0x08
//	rsr		a0, VPRI
//	s32i	a0, a2, 0x0C

//Save DEBUG_PC, but if the instruction is at DO_BREAK, skip over the (fixed) break instruction so
//we will not execute it again after returning.
	rsr		a0, DEBUG_PC
	movi	a1, do_break_breakpoint
	bne		a0, a1, is_not_do_break
	addi	a0, a0, 3

	movi a4, 0x60000000
	movi a3, 'I'
	s32i a3,a4, 0

is_not_do_break:
	s32i	a0, a2, 0x00
/*
	rsr	a2, ps
	addi	a2, a2, -PS_EXCM_MASK
	wsr	a2, ps
	rsync
*/
	movi a2, 0x60000000
	movi a3, 'X'
	s32i a3,a2, 0

	movi a1, exceptionStack+63*4

	call0		handle_debug_exception

DebugExceptionExit:
/*
	rsr	a2, ps
	addi	a2, a2, PS_EXCM_MASK
	wsr	a2, ps
	rsync
*/
	movi	a2, savedRegs
	l32i	a0, a2, 0x0C
//	wsr		a0, VPRI
//	l32i	a0, a2, 0x08
	wsr		a0, SAR
	l32i	a0, a2, 0x00
	wsr		a0, DEBUG_PC
	l32i	a3, a2, 0x18
	l32i	a4, a2, 0x1c
	l32i	a5, a2, 0x20
	l32i	a6, a2, 0x24
	l32i	a7, a2, 0x28
	l32i	a8, a2, 0x2c
	l32i	a9, a2, 0x30
	l32i	a10, a2, 0x34
	l32i	a11, a2, 0x38
	l32i	a12, a2, 0x3c
	l32i	a13, a2, 0x40
	l32i	a14, a2, 0x44
	l32i	a15, a2, 0x48
	l32i	a0, a2, 0x14
	wsr		a0, DEBUG_EXCSAVE //was R2
//	l32i	a0, a2, 0x04
//	wsr		a0, DEBUG_PS
	l32i	a1, a2, 0x04
	l32i	a0, a2, 0x10

	//Read back A2 value, put back address of this routine.
	movi	a2, DebugExceptionEntry
	xsr	a2, DEBUG_EXCSAVE

	//All done
	rfi	XCHAL_DEBUGLEVEL



	.global init_debug_entry
	.align	4
init_debug_entry:

	movi	a2, DebugExceptionEntry
	wsr		a2, DEBUG_EXCSAVE
//	movi	a2, RelocatedExceptionVector
//	wsr		a2, VECBASE


	ret


	.global do_break
	.align	4
do_break:
	addi	a1, a1, -16
	s32i	a15, a1, 12
	mov		a15, a1

do_break_breakpoint:
	break 0,0

	mov		a1, a15
	l32i	a15, a1, 12
	addi	a1, a1, 16
	ret


