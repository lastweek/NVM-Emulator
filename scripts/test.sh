#!/bin/bash
#
# Copyright (C) 2015-2016 Yizhou Shan <shanyizhou@ict.ac.cn>
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

_PREFIX=/home/syz/Github/NVM

CORE_PMU_MODULE=${_PREFIX}/core.ko
UNCORE_PMU_MODULE=${_PREFIX}/uncore.ko

CORE_IOCTL=/proc/core_pmu
UNCORE_IOCTL=/proc/uncore_pmu

INSTALL_MOD=insmod
REMOVE_MOD=rmmod

Start()
{
	${INSTALL_MOD} ${CORE_PMU_MODULE}
	${INSTALL_MOD} ${UNCORE_PMU_MODULE}
}

End()
{
	${REMOVE_MOD} ${CORE_PMU_MODULE}
	${REMOVE_MOD} ${UNCORE_PMU_MODULE}
}

declare -i bw
declare -i latency

Start

# Disbale, -32, -64, -128, -256
for ((latency = 0; latency <= 4; latency++)); do
	# Full bw, 1/2 bw, 1/4 bw
	for ((bw = 0; bw <= 4; bw += 2)); do

		#
		# Step I: Change the configuration of emulation
		#

		echo ${latency} > ${CORE_IOCTL}
		echo ${bw} > ${UNCORE_IOCTL}

		#
		# Step II: Customized NVM application evaluation
		#
		
		dir_name=spec_${latency}_${bw}
		if [ ! -d ${dir_name} ]; then
			mkdir -p ${dir_name}
		fi
		
		# GCC BZIP MCF BWAVES MILC
		SPEC_BENCH="403 401 429 410 433"
		SPEC_FLAGS="--config=mytest.cfg --noreportable --iteration=1"
		for i in ${SPEC_BENCH}; do
			file_name=${PWD}/${dir_name}/${latency}_${bw}_${i}
			runspec ${SPEC_FLAGS} ${i} > ${file_name} 2> ${file_name}_errorLog
			cat ${CORE_IOCTL} >> ${file_name}
			cat ${UNCORE_IOCTL} >> ${file_name}
		done
	done
done

End
