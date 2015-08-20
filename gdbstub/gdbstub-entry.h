void init_debug_entry();
void do_break();
void icount_ena_single_step();

int set_hw_breakpoint(int addr, int len);
int set_hw_watchpoint(int addr, int len, int type);
int del_hw_breakpoint(int addr);
int del_hw_watchpoint(int addr);
