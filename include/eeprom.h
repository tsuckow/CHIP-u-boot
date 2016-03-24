/*
 * Copyright (c) 2015 Free Electrons
 * Copyright (c) 2015 NextThing Co
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef __EEPROM_H
#define __EEPROM_H

#include <dm.h>

struct eeprom_ops {
	int	(*read_buf)(struct udevice *dev, unsigned offset,
			    u8 *buf, unsigned count);
};

int eeprom_read_buf(struct udevice *dev, unsigned offset,
		    u8 *buf, unsigned count);

#endif
