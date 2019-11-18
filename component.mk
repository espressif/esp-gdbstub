#
# Component Makefile
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

COMPONENT_EXTRA_CLEAN := gdbcmds

$(call compile_only_if,$(CONFIG_GDB_ENABLE),gdbstub.o)
$(call compile_only_if,$(CONFIG_GDB_ENABLE),gdbstub-entry.o)