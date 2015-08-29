#
#	Makefile for NVM Emulator System
#

obj-m += uncore_pmu.o
obj-m += core_pmu.o
obj-m += ioapic.o
obj-m += cpuctl.o

KERNEL_VERSION = /lib/modules/$(shell uname -r)/build/

all:
	make -C $(KERNEL_VERSION) M=$(PWD) modules
clean:
	make -C $(KERNEL_VERSION) M=$(PWD) clean
