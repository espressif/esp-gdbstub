#include "gdbstub.h"
#include "osapi.h"

#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "user_interface.h"
#include "mem.h"
#include "xtensa/corebits.h"
#include "gdbstub-entry.h"

#include "v7_gdb.h"

//From xtruntime-frames.h
struct XTensa_exception_frame_s {
	uint32_t pc;
	uint32_t ps;
	uint32_t sar;
	uint32_t vpri;
	uint32_t a0;
	uint32_t a[14]; //a2..a15
//These are added manually by the exception code; the HAL doesn't include these.
	uint32_t litbase;
	uint32_t sr176;
	uint32_t sr208;
	uint32_t a1;
	uint32_t reason;
};



//Not defined in include files...
int os_printf_plus(const char *format, ...)  __attribute__ ((format (printf, 1, 2)));
void _xtos_set_exception_handler(int cause, void (exhandler)(struct XTensa_exception_frame_s *frame));
void xthal_set_intenable(int en);
void _ResetVector();
extern void ets_wdt_disable(void);

#define REG_UART_BASE( i )  (0x60000000+(i)*0xf00)
#define UART_STATUS( i )                        (REG_UART_BASE( i ) + 0x1C)
#define UART_RXFIFO_CNT 0x000000FF
#define UART_RXFIFO_CNT_S 0
#define UART_TXFIFO_CNT 0x000000FF
#define UART_TXFIFO_CNT_S                   16
#define UART_FIFO( i )                          (REG_UART_BASE( i ) + 0x0)

#define PBUFLEN 256

static char chsum;

//The asm stub saves the Xtensa registers here when a debugging exception happens.
struct XTensa_exception_frame_s savedRegs;
//This is the debugging exception stack.
int exceptionStack[64];

static unsigned char cmd[PBUFLEN];

static int ICACHE_FLASH_ATTR gdbRecvChar() {
	int i;
	while (((READ_PERI_REG(UART_STATUS(0))>>UART_RXFIFO_CNT_S)&UART_RXFIFO_CNT)==0) ;
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

unsigned char readbyte(int p) {
	int *i=(int*)(p&(~3));
	if (p<0x20000000 || p>=0x60000000) return -1;
	return *i>>((p&3)*8);
}

void writeByte(int p, unsigned char d) {
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
	char *reason="break"; //default
	int i=0;
	ets_wdt_disable();
	xthal_set_intenable(0);
	gdbPacketStart();
	gdbPacketChar('T');
	gdbPacketHex(5, 8); //sigtrap
#if 0
	if (savedRegs.reason&(1<<0)) reason="break";
	if (savedRegs.reason&(1<<1)) reason="hwbreak";
	if (savedRegs.reason&(1<<2)) reason="watch";
	if (savedRegs.reason&(1<<3)) reason="swbreak";
	if (savedRegs.reason&(1<<4)) reason="swbreak";

	while(reason[i]!=0) {
		gdbPacketChar(reason[i]);
		i++;
	}
	gdbPacketChar(':');
#endif
	//ToDo: watch: send address
	gdbPacketEnd();
}



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
		//ToDo
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
		data++;
		j=gdbGetHexVal(&data, -1); //length
		data++;
		for (k=0; k<j; k++) {
			writeByte(i, gdbGetHexVal(&data, 2));
			i++;
		}
	} else if (cmd[0]=='?') {
		//Reply with stop reason
		sendReason();
//		gdbPacketStart();
//		gdbPacketChar('S');
//		gdbPacketHex(1, 8); //ToDo: figure out better reason, maybe link to exception type
//		gdbPacketEnd();
	} else if (cmd[0]=='c') {
		return ST_CONT;
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


void handle_debug_exception() {
	sendReason();
	while(gdbReadCommand()!=ST_CONT);
}



void gdb_semihost_putchar1() {

}

#define EXCEPTION_GDB_SP_OFFSET 0x100

static void gdb_exception_handler(struct XTensa_exception_frame_s *frame) {
	save_extra_sfrs_for_exception();
	os_memcpy(&savedRegs, frame, 19*4);
	//Credits go to Cesanta for this trick.
	savedRegs.a1=(uint32_t)frame+EXCEPTION_GDB_SP_OFFSET;
	
	while(gdbReadCommand()!=ST_CONT);
	os_memcpy(frame, &savedRegs, 19*4);
}

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

void do_c_exception() {
	volatile char *e=(volatile char*)1;
	*e=1;
}

void gdbstub_init() {
#if 1
//	os_install_putc1(gdb_semihost_putchar1);
	
//	ets_wdt_disable();
//	xthal_set_intenable(0);
//	while(1) gdbReadCommand();
	install_exceptions();
//	os_printf("Pre init_debug_entry\n");
	init_debug_entry();
//	os_printf("Post init_debug_entry\n");

#else
	gdb_init();
#endif
	os_printf("Executing do_break\n");
//	do_break();
	os_printf("Executing do_c_exception\n");
	do_c_exception();
	os_printf("Break done\n");
}

