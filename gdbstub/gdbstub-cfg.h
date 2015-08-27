#ifndef GDBSTUB_CFG_H
#define  GDBSTUB_CFG_H

/*
Enable this define if you're using the RTOS SDK. It will use a custom exception handler instead of the HAL
and do some other magic to make everything work and compile under FreeRTOS.
*/
#define FREERTOS

/*
Enable this to make the exception and debugging handlers switch to a private stack. This will use 
up 1K of RAM, but may be useful if you're debugging stack or stack pointer corruption problems. It's
normally disabled because not many situations need it. If for some reason the GDB communication 
stops when you run into an error in your code, try enabling this.
*/
#define GDBSTUB_USE_OWN_STACK


/*
Enabling this will redirect console output to GDB. This basically means that printf/os_printf output 
will show up in your gdb session, which is useful if you use gdb to do stuff. It also means that if
you use a normal terminal, you can't read the printfs anymore.
*/
//#define REDIRECT_CONSOLE_OUTPUT


/*
Enable this if you want the GDB stub to wait for you to attach GDB before running. It does this by
breaking in the init routine; use the gdb 'c' command (continue) to start the program.
*/
//#define BREAK_ON_INIT

#endif

