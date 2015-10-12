# Non-Volatile Memory Related System

Copyright (C) 2015 Yizhou Shan <shanyizhou@ict.ac.cn>

It is a NVM simulator. We do NOT have any chance to modify microcode of Intel.
Hence using Intel PMU to simulate NVM latency seems a feasible solution. This
simulator simulate the write and read latency of NVM, about 105ns and 65ns,
repectively. The bandwidth is simulated by using a transaction threshold of
IMC(Xeon E5 v2 or above has this feature).

For more information about NVM simulator, please read the PMFS paper and a MSST
paper: A study of application performance with Non-Volatile Main memory.

# Intel Uncore Performance Monitoring Unit

Intel® Xeon® processor E5 v3 family and Intel® Xeon® processor E7 v3 family
are based on Haswell-E. (CPUID DisplayFamily_DisplayModel = 06_3F)

Uncore performance monitors represent a per-socket resource that is not meant
to be affected by context switches and thread migration performed by the OS,
it is recommended that the monitoring software agent establish a fixed affinity
binding to prevent cross-talk of event counts from different uncore PMU.

The programming interface of the counter registers and control registers fall
into two address spaces:
	• Accessed by MSR are PMON registers within the Cbo units, SBo, PCU, and U-Box
	• Access by PCI device configuration space are PMON registers within the HA,
	  IMC, Intel QPI, R2PCIe and R3QPI units.

Note that, PCI-based uncore units in the Intel® Xeon® Processor E5 and E7 v3
Product Family can be found using bus, device and functions numbers. However,
the busno has to be found dynamically in each package. First, for each package,
it is necessary to read the node ID offset in the Ubox. That needs to match
the GID offset of the Ubox in a specific pattern to get the busno for the
package. This busno can then be used with the given D:F (device:function)
listed witheach box’s counters that are accessed through PCICfg space.

To manage the large number of counter registers distributed across many units
and collect event data efficiently, this section describes the hierarchical
technique to start/ stop/restart event counting that a software agent may need
to perform during a monitoring session.