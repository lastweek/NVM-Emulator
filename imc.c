#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>

static void bandwidth(void)
{
	struct pci_dev *imc;
	struct pci_bus *bus;

	imc = pci_get_device(0x8086, 0x2fb4, NULL);
	bus = imc->bus;
	print
}

static int imc_init(void)
{
	int cpu = smp_processor_id();

	

	

	return 0;
}

static void imc_exit(void)
{
	if (imc)
		pci_dev_put(imc);
}

module_init(imc_init);
module_exit(imc_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("shanyizhou@ict.ac.cn");
