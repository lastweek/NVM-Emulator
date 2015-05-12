sudo dmesg -C
sudo insmod pmc.ko
dmesg | grep PMC
sudo rmmod -f pmc
