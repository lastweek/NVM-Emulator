#!/bin/bash
##
# Script to emulate NVM latency and bandwidth with PMU modules.
# Default configuration:
# o Every core will recieve NMI interrupts in an interval of -256 LLC misses.
# o Memory throttling are enabled at all nodes, defaults to 1/1 full bandwidth.
##
# Copyright (C) 2015 Yizhou Shan <shanyizhou@ict.ac.cn>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

set -e

CORE_PMU_MODULE=core.ko
UNCORE_PMU_MODULE=uncore.ko

CORE_IOCTL=/proc/core_pmu
UNCORE_IOCTL=/proc/uncore_pmu

INSTALL_MOD=insmod
REMOVE_MOD=rmmod

Start_simulating()
{
	${INSTALL_MOD} ${CORE_PMU_MODULE}
	${INSTALL_MOD} ${UNCORE_PMU_MODULE}
}

End_simulating()
{
	${REMOVE_MOD} ${CORE_PMU_MODULE}
	${REMOVE_MOD} ${UNCORE_PMU_MODULE}
}

# GCC		403
# BZIP		401
# MCF		429
# BWAVES	410
# MILC		433
Test_spec2006()
{
	runspec --config=mytest.cfg --noreportable --iteration=1
}

declare -i bw
declare -i latency

Start_simulating

# Disbale, -32, -64, -128, -256
for ((latency = 0; latency <= 4; latency++)); do
	# Full bw, 1/2 bw, 1/4 bw
	for ((bw = 0; bw <= 4; bw += 2)); do
		echo ${latency} > ${CORE_IOCTL}
		echo ${bw} > ${UNCORE_IOCTL}
		
	done
done

End_simulating
