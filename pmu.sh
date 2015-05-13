sudo dmesg -C
sudo insmod pmu.ko
dmesg | grep PMU
sudo rmmod -f pmu
