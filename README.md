# Non-Volatile Memory Related System

Copyright (C) 2015 Yizhou Shan <shanyizhou@ict.ac.cn>

Here is a NVM simulator.

Basically, NVM differs from DRAM from two things:
	1)

## Intel Uncore Performance Monitoring Unit

Uncore performance monitors represent a per-socket resource that is not meant
to be affected by context switches and thread migration performed by the OS,
it is recommended that the monitoring software agent establish a fixed affinity
binding to prevent cross-talk of event counts from different uncore PMU.

The programming interface of the counter registers and control registers fall
into two address spaces:
 1) Accessed by MSR are PMON registers within the Cbo units, SBo, PCU, and U-Box
 2) Access by PCI device configuration space are PMON registers within the HA,
    IMC, Intel QPI, R2PCIe and R3QPI units.

Note that, PCI-based uncore units in the Intel® Xeon® Processor E5 and E7 v3
Product Family can be found using bus, device and functions numbers. However,
the busno has to be found dynamically in each package. First, for each package,
it is necessary to read the node ID offset in the Ubox. That needs to match
the GID offset of the Ubox in a specific pattern to get the busno for the
package. This busno can then be used with the given D:F (device:function)
listed witheach box’s counters that are accessed through PCI config space.

## Intel Core Performance Monitoring Unit

It is impossible to illustrate the Intel PMU in such a readme file. For more
information about Core PMU or Uncore PMU, please consult Intel SDM and some
Intel PMU guides.

## License

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
