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

/*
 * This file describes methods to manipulate Integrated Memory Controller (IMC)
 */

#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

/*
 * Device 20, 21, 22 Function 0, 1
 * IMC0, Channel 0-1 --> 20:0 20:1 (2fb4 2fb5)
 * IMC0, Channel 2-3 --> 21:0 21:1 (2fb0 2fb1)
 * IMC1, Channel 0-1 --> 23:0 23:1 (2fd4 2fd5)
 */
struct pci_dev *imc;
