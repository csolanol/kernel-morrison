/*
 * Copyright (C) 2009 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#ifndef _LINUX_SFH7743_H_
#define _LINUX_SFH7743_H_

#include <linux/ioctl.h>

#ifdef __KERNEL__

#ifdef CONFIG_ARCH_MSM
struct sfh7743_platform_data {
	const char *name;
	unsigned    gpio_en;
	const char *vreg_en;
	unsigned    gpio_intr;
};
#else
struct sfh7743_platform_data {
	int (*init)(void);
	void (*exit)(void);
	int (*power_on)(void);
	int (*power_off)(void);

	int gpio;
} __attribute__ ((packed));
#endif

#endif /* __KERNEL__ */

#define SFH7743_IO			0xA2

#define SFH7743_IOCTL_GET_ENABLE		_IOR(SFH7743_IO, 0x00, char)
#define SFH7743_IOCTL_SET_ENABLE		_IOW(SFH7743_IO, 0x01, char)
#define SFH7743_IOCTL_GET_PRESENCE     	_IOR(SFH7743_IO, 0x02, char)
#define SFH7743_IOCTL_PUSH_PRESENCE    	_IOW(SFH7743_IO, 0x03, char)
#define SFH7743_IOCTL_GET_STATUS        _IOR(SFH7743_IO, 0x04, char)
#define SFH7743_IOCTL_DISABLE			_IOW(SFH7743_IO, 0x05, char)
#define SFH7743_IOCTL_ENABLE			_IOW(SFH7743_IO, 0x06, char)


#endif /* _LINUX_SFH7743_H__ */
