#include "gdbstub.h"
#include "osapi.h"

static struct regfile currRegs;

void gdb_semihost_putchar1() {

}

void gdb_exception_handler(struct xtos_saved_regs *frame) {
	currRegs.a[0]=frame->a0
	currRegs.a[1]=
}

static void install_exceptions() {
	int i;
	int exno[]={EXCCAUSE_ILLEGAL, EXCCAUSE_SYSCALL, EXCCAUSE_INSTR_ERROR, EXCCAUSE_LOAD_STORE_ERROR,
			EXCCAUSE_DIVIDE_BY_ZERO, EXCCAUSE_UNALIGNED, EXCCAUSE_INSTR_DATA_ERROR, EXCCAUSE_LOAD_STORE_DATA_ERROR, 
			EXCCAUSE_INSTR_ADDR_ERROR, EXCCAUSE_LOAD_STORE_ADDR_ERROR, EXCCAUSE_INSTR_PROHIBITED,
			EXCCAUSE_LOAD_PROHIBITED, EXCCAUSE_STORE_PROHIBITED};
	for (i=0; i<sizeof(exno)/sizeof(int); i++) _xtos_set_exception_handler(exno[i], gdb_exception_handler);
}


void gdbstub_init() {
	os_install_putc1(gdb_semihost_putchar1);
}