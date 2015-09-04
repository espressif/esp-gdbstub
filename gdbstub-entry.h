#ifndef GDBSTUB_ENTRY_H
#define GDBSTUB_ENTRY_H

void gdbstub_init_debug_entry();
void gdbstub_do_break();
void gdbstub_icount_ena_single_step();

int gdbstub_set_hw_breakpoint(int addr, int len);
int gdbstub_set_hw_watchpoint(int addr, int len, int type);
int gdbstub_del_hw_breakpoint(int addr);
int gdbstub_del_hw_watchpoint(int addr);

#endif