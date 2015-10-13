#
#	Makefile for NVM Emulator System
#

# Composite module
obj-m := uncore.o

uncore-y := uncore_pmu.o
uncore-y += uncore_hswep.o
uncore-y += uncore_imc.o
uncore-y += uncore_proc.o

KERNEL_VERSION = /lib/modules/$(shell uname -r)/build/

all:
	make -C $(KERNEL_VERSION) M=$(PWD) modules
clean:
	make -C $(KERNEL_VERSION) M=$(PWD) clean

help:
	make -C $(KERNEL_VERSION) M=$(PWD) help
