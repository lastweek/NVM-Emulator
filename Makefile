obj-m += pmu.o

KERNEL_VERSION = /lib/modules/$(shell uname -r)/build/

all:
	make -C $(KERNEL_VERSION) M=$(PWD) modules
clean:
	make -C $(KERNEL_VERSION) M=$(PWD) clean
