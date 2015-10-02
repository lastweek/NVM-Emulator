/*
 *	Copyright (C) 2015 Yizhou Shan <shanyizhou@ict.ac.cn>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License along
 *	with this program; if not, write to the Free Software Foundation, Inc.,
 *	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/***
 *
 *	Platform:		Xeon® E5 v3 and Xeon® E7 v3
 *	Microarchitecture:	Haswell-EP, Haswell-EX
 *
 ***
 *
 *	Platform:		Xeon® E5 v2 and Xeon® E7 v2
 *	Microarchitecture:	Ivy Bridge-EP, Ivy Bridge-EX
 *	MSR Description:
 *	(Intel SDM Volume 3, Chapter 35.8 MSRS IN INTEL® PROCESSOR FAMILY
 *	 BASED ON INTEL® MICROARCHITECTURE CODE NAME IVY BRIDGE)
 *
 *	Platform:		Xeon® E5 v1
 *	Microarchitecture:	Sandy Bridge-EP
 *	MSR Description:
 *	(Intel SDM Volume 3, Chapter 35.8 MSRS IN INTEL® PROCESSOR FAMILY
 *	 BASED ON INTEL® MICROARCHITECTURE CODE NAME SANDY BRIDGE)
 *
 *	Platform:		Xeon® E7 v1
 *	Microarchitecture:	Westmere-EX
 *	MSR Description:
 *
 ***
 *
 * Wikipedia Xeon for more detailed information.
 * Consult Intel Software Developer Manual and PMU Guide for details.
 */

struct uncore_pmu_desc {
	char name[64];
};

struct uncore_box {

};
