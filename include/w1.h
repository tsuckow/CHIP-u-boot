/*
 * Copyright (c) 2015 Free Electrons
 * Copyright (c) 2015 NextThing Co
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef __W1_H
#define __W1_H

#include <dm.h>

#define W1_FAMILY_DS2431	0x2d

/**
 * struct w1_driver_entry - Matches a driver to its w1 family
 * @driver: Driver to use
 * @family: W1 family handled by this driver
 */
struct w1_driver_entry {
	struct driver	*driver;
	u8		family;
};

#define U_BOOT_W1_DEVICE(__name, __family)				\
	ll_entry_declare(struct w1_driver_entry, __name, w1_driver_entry) = { \
		.driver = llsym(struct driver, __name, driver),		\
		.family = __family,					\
	}

struct w1_ops {
	u8	(*read_byte)(struct udevice *dev);
	bool	(*reset)(struct udevice *dev);
	u8	(*triplet)(struct udevice *dev, bool bdir);
	void	(*write_byte)(struct udevice *dev, u8 byte);
};

int w1_get_bus(int busnum, struct udevice **busp);
u8 w1_get_device_family(struct udevice *dev);

int w1_read_buf(struct udevice *dev, u8 *buf, unsigned count);
int w1_read_byte(struct udevice *dev);
int w1_reset_select(struct udevice *dev);
int w1_write_buf(struct udevice *dev, u8 *buf, unsigned count);
int w1_write_byte(struct udevice *dev, u8 byte);

#endif
