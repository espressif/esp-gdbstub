
GDBSTUB
=======

Intro
-----

While the ESP8266 supports the standard Gnu set of C programming utilities, for now the choice of debuggers
has been limited: there is an attempt at [OpenOCD support](https://github.com/projectgus/openocd), but at 
the time of writing, it doesn't support hardware watchpoints and breakpoints yet, and it needs a separate
JTAG adapter connecting to the ESP8266s JTAG pins. As an alternative, [Cesanta](https://www.cesanta.com/) 
has implemented a barebones[GDB stub](https://blog.cesanta.com/esp8266-gdb) in their Smart.js solution -
unfortunately, this only supports exception catching and needs some work before you can use it outside of 
the Smart.js platform. Moreover, it also does not work with FreeRTOS.

For internal use, we at Espressif desired a GDB stub that works with FreeRTOS and is a bit more capable,
so we designed our own implementation of it. This stub works under FreeRTOS and is able to catch exceptions
and do backtraces on them, read and write memory, forward [os_]printf statements to gdb, single-step 
instructions and set hardware break- and watchpoints. It connects to the host machine (which runs gdb) 
using the standard serial connection that's also used for programming.

In order to be useful the gdbstub has to be used in conjunction with an xtensa-lx106-elf-gdb, which a link
can be found on this page: https://github.com/espressif/ESP8266_RTOS_SDK/tree/release/v3.2

Integration
-----
 * Go to your components folder in your project
 * Type ``git submodule add git@github.com:sideralis/esp-gdbstub.git``
 * If you have a component.mk in components folder root, you may have to add the esp-gdbstub folder to ``COMPONENT_SRCDIRS``
 * Configure gdb with make menuconfig

Usage with xtensa gdb
-----
 * Include in your code a call to ``gdbstub_init()``
 * ``make flash``
 * ``make gdb``
 * The code will be stop inside function ``gdbstub_init()``, use n commands to step over.
 
Usage with Eclipse
-----
 * Prerequisite: 
    * install C/C++ Remote (over TCF/TE) Run/Debug Launch which can be found in Mobile and Device Development or Linux Tools
    * xtensa gdb is in your path
 * In C/C++ Projects view, right click on your project name and select Debug As/Debug Configurations...
 * In C/C++ Remote Application, add a new launch configuration
 * In ... TBD
 
 * Configure gdbstub by editting `gdbstub-cfg.h`. There are a bunch of options you can tweak: FreeRTOS or bare SDK,
private exception/breakpoint stack, console redirection to GDB, wait till debugger attachment etc. You can also
configure the options by including the proper -Dwhatever gcc flags in your Makefiles.
 * In your user_main.c, add an `#include <../gdbstub/gdbstub.h>` and call `gdbstub_init();` somewhere in user_main.
 * Compile and flash your board.
 * Run gdb, depending on your configuration immediately after resetting the board or after it has run into
an exception. The easiest way to do it is to use the provided script: xtensa-lx106-elf-gdb -x gdbcmds -b 38400
Change the '38400' into the baud rate your code uses. You may need to change the gdbcmds script to fit the
configuration of your hardware and build environment.

Notes
-----
 * Using software breakpoints ('br') only works on code that's in RAM. Code in flash can only have a hardware
breakpoint ('hbr').
 * Due to hardware limitations, only one hardware breakpoint and one hardware watchpoint are available.
 * Pressing control-C to interrupt the running program depends on gdbstub hooking the UART interrupt.
If some code re-hooks this afterwards, gdbstub won't be able to receive characters. If gdbstub handles
the interrupt, the user code will not receive any characters.
 * Continuing from an exception is not (yet) supported in FreeRTOS mode.
 * The WiFi hardware is designed to be serviced by software periodically. It has some buffers so it
will behave OK when some data comes in while the processor is busy, but these buffers are not infinite.
If the WiFi hardware receives lots of data while the debugger has stopped the CPU, it is bound
to crash. This will happen mostly when working with UDP and/or ICMP; TCP-connections in general will
not send much more data when the other side doesn't send any ACKs.

License
-------
This gdbstub is licensed under the Espressif MIT license, as described in the License file.


Thanks
------
 * Cesanta, for their initial ESP8266 exception handling only gdbstub,
 * jcmvbkbc, for providing an incompatible but interesting gdbstub for other Xtensa CPUs,
 * Sysprogs (makers of VisualGDB), for their suggestions and bugreports.
