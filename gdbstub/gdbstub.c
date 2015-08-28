#include "gdbstub.h"

#include "ets_sys.h"
#include "gpio.h"
#include "os_type.h"
#include "user_interface.h"
#include "mem.h"
#include "xtensa/corebits.h"
#include "gdbstub-entry.h"
#include "gdbstub-cfg.h"

extern void ets_wdt_disable(void);
extern void ets_wdt_enable(void);

#ifdef FREERTOS
#include <string.h>
#include <stdio.h>
#define os_printf(...) printf(__VA_ARGS__)
//#define ets_wdt_disable() while (0) {}
//#define ets_wdt_enable() while (0) {}
#define os_memcpy(a,b,c) memcpy(a,b,c)
#else
#include "osapi.h"
int os_printf_plus(const char *format, ...)  __attribute__ ((format (printf, 1, 2)));
#endif


//From xtruntime-frames.h
struct XTensa_exception_frame_s {
	uint32_t pc;
	uint32_t ps;
	uint32_t sar;
	uint32_t vpri;
	uint32_t a0;
	uint32_t a[14]; //a2..a15
//These are added manually by the exception code; the HAL doesn't set these on an exception.
	uint32_t litbase;
	uint32_t sr176;
	uint32_t sr208;
	uint32_t a1;
	 //'reason' is abused for both the debug and the exception vector: if bit 7 is set,
	//this contains an exception reason, otherwise it contains a debug vector bitmap.
	uint32_t reason;
};



//Not defined in include files...
void _xtos_set_exception_handler(int cause, void (exhandler)(struct XTensa_exception_frame_s *frame));
void xthal_set_intenable(int en);
void _ResetVector();

#define REG_UART_BASE( i )  (0x60000000+(i)*0xf00)
#define UART_STATUS( i )                        (REG_UART_BASE( i ) + 0x1C)
#define UART_RXFIFO_CNT 0x000000FF
#define UART_RXFIFO_CNT_S 0
#define UART_TXFIFO_CNT 0x000000FF
#define UART_TXFIFO_CNT_S                   16
#define UART_FIFO( i )                          (REG_UART_BASE( i ) + 0x0)

#define PBUFLEN 256
#define OBUFLEN 32

static char chsum;

//The asm stub saves the Xtensa registers here when a debugging exception happens.
struct XTensa_exception_frame_s savedRegs;
#ifdef GDBSTUB_USE_OWN_STACK
//This is the debugging exception stack.
int exceptionStack[256];
#endif

static unsigned char cmd[PBUFLEN];
static unsigned char obuf[OBUFLEN];
static int obufpos=0;


static int keepWDTalive() {
	uint64_t *wdtval=(uint64_t*)0x3ff21048;
	uint64_t *wdtovf=(uint64_t*)0x3ff210cc;
	int *wdtctl=(int*)0x3ff210c8;
	*wdtovf=*wdtval+1600000;
	*wdtctl|=(1<<31);
}

static int ICACHE_FLASH_ATTR gdbRecvChar() {
	int i;
	while (((READ_PERI_REG(UART_STATUS(0))>>UART_RXFIFO_CNT_S)&UART_RXFIFO_CNT)==0) {
		keepWDTalive();
	}
	i=READ_PERI_REG(UART_FIFO(0));
	return i;
}

static void gdbSendChar(char c) {
	while (((READ_PERI_REG(UART_STATUS(0))>>UART_TXFIFO_CNT_S)&UART_TXFIFO_CNT)>=126) ;
	WRITE_PERI_REG(UART_FIFO(0), c);
}

static struct regfile currRegs;

void gdbPacketStart() {
	chsum=0;
	gdbSendChar('$');
}

void gdbPacketChar(char c) {
	if (c=='#' || c=='$' || c=='}' || c=='*') {
		gdbSendChar('}');
		gdbSendChar(c^0x20);
		chsum+=(c^0x20)+'}';
	} else {
		gdbSendChar(c);
		chsum+=c;
	}
}

void gdbPacketStr(char *c) {
	while (*c!=0) {
		gdbPacketChar(*c);
		c++;
	}
}

void gdbPacketHex(int val, int bits) {
	char hexChars[]="0123456789abcdef";
	int i;
	for (i=bits; i>0; i-=4) {
		gdbPacketChar(hexChars[(val>>(i-4))&0xf]);
	}
}

void gdbPacketEnd() {
	gdbSendChar('#');
	gdbPacketHex(chsum, 8);
}


#define ST_ENDPACKET -1
#define ST_ERR -2
#define ST_OK -3
#define ST_CONT -4

long gdbGetHexVal(unsigned char **ptr, int bits) {
	int i;
	int no;
	unsigned int v=0;
	char c;
	no=bits/4;
	if (bits==-1) no=64;
	for (i=0; i<no; i++) {
		c=**ptr;
		(*ptr)++;
		if (c>='0' && c<='9') {
			v<<=4;
			v|=(c-'0');
		} else if (c>='A' && c<='F') {
			v<<=4;
			v|=(c-'A')+10;
		} else if (c>='a' && c<='f') {
			v<<=4;
			v|=(c-'a')+10;
		} else if (c=='#') {
			if (bits==-1) {
				(*ptr)--;
				return v;
			}
			return ST_ENDPACKET;
		} else {
			if (bits==-1) {
				(*ptr)--;
				return v;
			}
			return ST_ERR;
		}
	}
	return v;
}

int gdbGetCharVal() {
	char c;
	c=gdbRecvChar();
	if (c=='#') return ST_ENDPACKET;
	if (c=='}') {
		c=gdbRecvChar();
		c^=0x20;
	}
	return c;
}

int iswap(int i) {
	int r;
	r=((i>>24)&0xff);
	r|=((i>>16)&0xff)<<8;
	r|=((i>>8)&0xff)<<16;
	r|=((i>>0)&0xff)<<24;
	return r;
}

unsigned char readbyte(unsigned int p) {
	int *i=(int*)(p&(~3));
	if (p<0x20000000 || p>=0x60000000) return -1;
	return *i>>((p&3)*8);
}

void writeByte(unsigned int p, unsigned char d) {
	int *i=(int*)(p&(~3));
	if (p<0x20000000 || p>=0x60000000) return;
	if ((p&3)==0) *i=(*i&0xffffff00)|(d<<0);
	if ((p&3)==1) *i=(*i&0xffff00ff)|(d<<8);
	if ((p&3)==2) *i=(*i&0xff00ffff)|(d<<16);
	if ((p&3)==3) *i=(*i&0x00ffffff)|(d<<24);
}

// https://sourceware.org/gdb/onlinedocs/gdb/Overview.html#Overview
// http://citeseerx.ist.psu.edu/viewdoc/download;jsessionid=257B772D871748F229AC82590EE321F6?doi=10.1.1.464.1563&rep=rep1&type=pdf


/*
 * Register file in the format lx106 gdb port expects it.
 *
 * Inspired by gdb/regformats/reg-xtensa.dat from
 * https://github.com/jcmvbkbc/crosstool-NG/blob/lx106-g%2B%2B/overlays/xtensa_lx106.tar
 */
struct regfile {
	uint32_t a[16];
	uint32_t pc;
	uint32_t sar;
	uint32_t litbase;
	uint32_t sr176;
	uint32_t sr208;
	uint32_t ps;
};


void sendReason() {
	char *reason=""; //default
	//exception-to-signal mapping
	char exceptionSignal[]={4,31,11,11,2,6,8,0,6,7,0,0,7,7,7,7};
	int i=0;
	gdbPacketStart();
	gdbPacketChar('T');
	if (savedRegs.reason&0x80) {
		i=savedRegs.reason&0x7f;
		if (i<sizeof(exceptionSignal)) return gdbPacketHex(exceptionSignal[i], 8); else gdbPacketHex(11, 8);
	} else {
		gdbPacketHex(5, 8); //sigtrap
//Current Xtensa GDB versions don't seem to request this, so let's leave it off.
#if 0
		if (savedRegs.reason&(1<<0)) reason="break";
		if (savedRegs.reason&(1<<1)) reason="hwbreak";
		if (savedRegs.reason&(1<<2)) reason="watch";
		if (savedRegs.reason&(1<<3)) reason="swbreak";
		if (savedRegs.reason&(1<<4)) reason="swbreak";

		gdbPacketStr(reason);
		gdbPacketChar(':');
		//ToDo: watch: send address
#endif
	}
	gdbPacketEnd();
}

static int hwBreakpointUsed=0;
static int hwWatchpointUsed=0;

int gdbHandleCommand(unsigned char *cmd, int len) {
	//Handle a command
	int i, j, k;
	unsigned char *data=cmd+1;
	if (cmd[0]=='g') {
		gdbPacketStart();
		gdbPacketHex(iswap(savedRegs.a0), 32);
		gdbPacketHex(iswap(savedRegs.a1), 32);
		for (i=2; i<16; i++) gdbPacketHex(iswap(savedRegs.a[i-2]), 32);
		gdbPacketHex(iswap(savedRegs.pc), 32);
		gdbPacketHex(iswap(savedRegs.sar), 32);
		gdbPacketHex(iswap(savedRegs.litbase), 32);
		gdbPacketHex(iswap(savedRegs.sr176), 32);
		gdbPacketHex(0, 32);
		gdbPacketHex(iswap(savedRegs.ps), 32);
		gdbPacketEnd();
	} else if (cmd[0]=='G') {
		savedRegs.a0=iswap(gdbGetHexVal(&data, 32));
		savedRegs.a1=iswap(gdbGetHexVal(&data, 32));
		for (i=2; i<16; i++) savedRegs.a[i-2]=iswap(gdbGetHexVal(&data, 32));
		savedRegs.pc=iswap(gdbGetHexVal(&data, 32));
		savedRegs.sar=iswap(gdbGetHexVal(&data, 32));
		savedRegs.litbase=iswap(gdbGetHexVal(&data, 32));
		savedRegs.sr176=iswap(gdbGetHexVal(&data, 32));
		gdbGetHexVal(&data, 32);
		savedRegs.ps=iswap(gdbGetHexVal(&data, 32));
		gdbPacketStart();
		gdbPacketStr("OK");
		gdbPacketEnd();
	} else if (cmd[0]=='m') {
		i=gdbGetHexVal(&data, -1);
		data++;
		j=gdbGetHexVal(&data, -1);
		gdbPacketStart();
		for (k=0; k<j; k++) {
			gdbPacketHex(readbyte(i++), 8);
		}
		gdbPacketEnd();
	} else if (cmd[0]=='M') {
		i=gdbGetHexVal(&data, -1); //addr
		data++; //skip ,
		j=gdbGetHexVal(&data, -1); //length
		data++; //skip :
		for (k=0; k<j; k++) {
			writeByte(i, gdbGetHexVal(&data, 8));
			i++;
		}
		gdbPacketStart();
		gdbPacketStr("OK");
		gdbPacketEnd();
	} else if (cmd[0]=='?') {
		//Reply with stop reason
		sendReason();
//	} else if (strncmp(cmd, "vCont?", 6)==0) {
//		gdbPacketStart();
//		gdbPacketStr("vCont;c;s");
//		gdbPacketEnd();
	} else if (strncmp(cmd, "vCont;c", 7)==0 || cmd[0]=='c') {
		return ST_CONT;
	} else if (strncmp(cmd, "vCont;s", 7)==0 || cmd[0]=='s') {
		icount_ena_single_step();
		return ST_CONT;
	} else if (cmd[0]=='q') {
		if (strncmp(&cmd[1], "Supported", 9)==0) {
			gdbPacketStart();
			gdbPacketStr("swbreak+;hwbreak+;PacketSize=255");
			gdbPacketEnd();
		} else {
			gdbPacketStart();
			gdbPacketEnd();
			return ST_ERR;
		}
	} else if (cmd[0]=='Z') {
		data+=2; //skip 'x,'
		i=gdbGetHexVal(&data, -1);
		data++; //skip ','
		j=gdbGetHexVal(&data, -1);
		gdbPacketStart();
		if (cmd[1]=='1') {
			if (set_hw_breakpoint(i, j)) {
				gdbPacketStr("OK");
			} else {
				gdbPacketStr("E01");
			}
		} else if (cmd[1]=='2' || cmd[1]=='3' || cmd[1]=='4') {
			int access=0;
			int mask=0;
			if (cmd[1]=='2') access=2; //write
			if (cmd[1]=='3') access=1; //read
			if (cmd[1]=='4') access=3; //access
			if (j==1) mask=0x3F;
			if (j==2) mask=0x3E;
			if (j==4) mask=0x3C;
			if (j==8) mask=0x38;
			if (j==16) mask=0x30;
			if (j==32) mask=0x20;
			if (j==64) mask=0x00;
			if (mask!=0 && set_hw_watchpoint(i,mask, access)) {
				gdbPacketStr("OK");
			} else {
				gdbPacketStr("E01");
			}
		}
		gdbPacketEnd();
	} else if (cmd[0]=='z') {
		data+=2; //skip 'x,'
		i=gdbGetHexVal(&data, -1);
		data++; //skip ','
		j=gdbGetHexVal(&data, -1);
		gdbPacketStart();
		if (cmd[1]=='1') {
			if (del_hw_breakpoint(i)) {
				gdbPacketStr("OK");
			} else {
				gdbPacketStr("E01");
			}
		} else if (cmd[1]=='2' || cmd[1]=='3' || cmd[1]=='4') {
			if (del_hw_watchpoint(i)) {
				gdbPacketStr("OK");
			} else {
				gdbPacketStr("E01");
			}
		}
		gdbPacketEnd();
	} else {
		gdbPacketStart();
		gdbPacketEnd();
		return ST_ERR;
	}
	return ST_OK;
}


//Lower layer: grab a command packet and check the checksum
//Calls gdbHandleCommand on the packet if the checksum is OK
//Returns ST_OK on success, ST_ERR when checksum fails, a 
//character if it is received instead of the GDB packet
//start char.
int gdbReadCommand() {
	unsigned char c;
	unsigned char chsum=0, rchsum;
	unsigned char sentchs[2];
	int p=0;
	unsigned char *ptr;
	c=gdbRecvChar();
	if (c!='$') return c;
	while(1) {
		c=gdbRecvChar();
		if (c=='#') {
			cmd[p]=0;
			break;
		}
		chsum+=c;
		if (c=='$') {
			//Wut, restart packet?
			chsum=0;
			p=0;
			continue;
		}
		if (c=='}') {
			c=gdbRecvChar();
			chsum+=c;
			c^=0x20;
		}
		cmd[p++]=c;
		if (p>=PBUFLEN) return ST_ERR;
	}
	//A # has been received. Get and check the received chsum.
	sentchs[0]=gdbRecvChar();
	sentchs[1]=gdbRecvChar();
	ptr=&sentchs[0];
	rchsum=gdbGetHexVal(&ptr, 8);
//	os_printf("c %x r %x\n", chsum, rchsum);
	if (rchsum!=chsum) {
		gdbSendChar('-');
		return ST_ERR;
	} else {
		gdbSendChar('+');
		return gdbHandleCommand(cmd, p);
	}
}

unsigned int getaregval(int reg) {
	if (reg==0) return savedRegs.a0;
	if (reg==1) return savedRegs.a1;
	return savedRegs.a[reg-2];
}

void setaregval(int reg, unsigned int val) {
	os_printf("%x -> %x\n", val, reg);
	if (reg==0) savedRegs.a0=val;
	if (reg==1) savedRegs.a1=val;
	savedRegs.a[reg-2]=val;
}

//Emulate the l32i/s32i instruction we're stopped at.
emulLdSt() {
	unsigned char i0=readbyte(savedRegs.pc);
	unsigned char i1=readbyte(savedRegs.pc+1);
	unsigned char i2=readbyte(savedRegs.pc+2);
	int *p;
	if ((i0&0xf)==2 && (i1&0xf0)==0x20) {
		//l32i
		p=(int*)getaregval(i1&0xf)+(i2*4);
		setaregval(i0>>4, *p);
		savedRegs.pc+=3;
	} else if ((i0&0xf)==0x8) {
		//l32i.n
		p=(int*)getaregval(i1&0xf)+((i1>>4)*4);
		setaregval(i0>>4, *p);
		savedRegs.pc+=2;
	} else if ((i0&0xf)==2 && (i1&0xf0)==0x60) {
		//s32i
		p=(int*)getaregval(i1&0xf)+(i2*4);
		*p=getaregval(i0>>4);
		savedRegs.pc+=3;
	} else if ((i0&0xf)==0x9) {
		//s32i.n
		p=(int*)getaregval(i1&0xf)+((i1>>4)*4);
		*p=getaregval(i0>>4);
		savedRegs.pc+=2;
	} else {
		os_printf("GDBSTUB: No l32i/s32i instruction: %x %x %x. Huh?", i2, i1, i0);
	}
}


void handle_debug_exception() {
	xthal_set_intenable(0);
	ets_wdt_disable();
	sendReason();
	while(gdbReadCommand()!=ST_CONT);
	if ((savedRegs.reason&0x84)==0x4) {
		//We stopped due to a watchpoint. We can't re-execute the current instruction
		//because it will happily re-trigger the same watchpoint, so we emulate it 
		//while we're still in debugger space.
		emulLdSt();
	}
	ets_wdt_enable();
	xthal_set_intenable(1);
}


#ifdef FREERTOS
void handle_user_exception() {
	xthal_set_intenable(0);
	ets_wdt_disable();
	savedRegs.reason|=0x80; //mark as an exception reason
	sendReason();
	while(gdbReadCommand()!=ST_CONT);
	if ((savedRegs.reason&0x84)==0x4) {
		//We stopped due to a watchpoint. We can't re-execute the current instruction
		//because it will happily re-trigger the same watchpoint, so we emulate it 
		//while we're still in debugger space.
		emulLdSt();
	}
	ets_wdt_enable();
	xthal_set_intenable(1);
}
#else

#define EXCEPTION_GDB_SP_OFFSET 0x100

static void gdb_exception_handler(struct XTensa_exception_frame_s *frame) {
	save_extra_sfrs_for_exception();
	os_memcpy(&savedRegs, frame, 19*4);
	//Credits go to Cesanta for this trick.
	savedRegs.a1=(uint32_t)frame+EXCEPTION_GDB_SP_OFFSET;
	savedRegs.reason|=0x80; //mark as an exception reason

	xthal_set_intenable(0);
	ets_wdt_disable();
	sendReason();
	while(gdbReadCommand()!=ST_CONT);
	ets_wdt_enable();
	xthal_set_intenable(1);

	os_memcpy(frame, &savedRegs, 19*4);
}
#endif

#ifdef REDIRECT_CONSOLE_OUTPUT
void gdb_semihost_putchar1(char c) {
	int i;
	obuf[obufpos++]=c;
	if (c=='\n' || obufpos==OBUFLEN) {
		gdbPacketStart();
		gdbPacketChar('O');
		for (i=0; i<obufpos; i++) gdbPacketHex(obuf[i], 8);
		gdbPacketEnd();
		obufpos=0;
	}
}
#endif

#ifndef FREERTOS
static void install_exceptions() {
	int i;
	int exno[]={EXCCAUSE_ILLEGAL, EXCCAUSE_SYSCALL, EXCCAUSE_INSTR_ERROR, EXCCAUSE_LOAD_STORE_ERROR,
			EXCCAUSE_DIVIDE_BY_ZERO, EXCCAUSE_UNALIGNED, EXCCAUSE_INSTR_DATA_ERROR, EXCCAUSE_LOAD_STORE_DATA_ERROR, 
			EXCCAUSE_INSTR_ADDR_ERROR, EXCCAUSE_LOAD_STORE_ADDR_ERROR, EXCCAUSE_INSTR_PROHIBITED,
			EXCCAUSE_LOAD_PROHIBITED, EXCCAUSE_STORE_PROHIBITED};
	for (i=0; i<(sizeof(exno)/sizeof(exno[0])); i++) {
		_xtos_set_exception_handler(exno[i], gdb_exception_handler);
	}
}
#else
extern void user_fatal_exception_handler();
extern void UserExceptionEntry();

static void install_exceptions() {
	//Replace the user_fatal_exception_handler by a jump to our own code
	int *ufe=(int*)user_fatal_exception_handler;
	//This mess encodes as a relative jump instruction to user_fatal_exception_handler
	*ufe=((((int)UserExceptionEntry-(int)user_fatal_exception_handler)-4)<<6)|6;
}
#endif


extern void user_fatal_exception_handler();
extern void UserExceptionEntry();

void gdbstub_init() {
#ifdef REDIRECT_CONSOLE_OUTPUT
	os_install_putc1(gdb_semihost_putchar1);
#endif
#ifndef FREERTOS
	install_exceptions();
#endif

	init_debug_entry();
#ifdef BREAK_ON_INIT
	do_break();
#endif
}

