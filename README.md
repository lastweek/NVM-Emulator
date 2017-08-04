#Non-Volatile Memory Emulator
#####Copyright (C) 2015-2016 Yizhou Shan <shanyizhou@ict.ac.cn>

(Project done in ICT. Not maintained anymore.)

The basic idea is to use Intel PMU to count number of read/writes requests  
issued from CPU to Memory. Based on this, the emulator manually inject delays  
to the CPU by running an idle function to emulate the extra read/write latency  
of NVM. The bandwidth is emulated by limiting the memory controller requests.  

For anyone who interested in this project:  
This emulator is much more complex that it shoud be. We SHOULD leverage the  
PMU facility already provided by linux kernel. I saw kernel-events improved  
a lot during These 2 or 3 years. Previously all x86-pmu-events code is  
in arch/x86/kernel/cpu/, now they have a standalone directory in arch/x86/events/.  

Anyway, let us see when toothpaste company Intel will have PM ready for markets.
