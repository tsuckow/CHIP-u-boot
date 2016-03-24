/*
 * Copyright (c) 2015 Free Electrons
 * Copyright (c) 2015 NextThing Co
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <linux/err.h>
#include <dm.h>
#include <eeprom.h>
#include <w1.h>

#define W1_F2D_READ_EEPROM	0xf0

static int ds2431_read_buf(struct udevice *dev, unsigned offset,
			   u8 *buf, unsigned count)
{
	w1_reset_select(dev);

	w1_write_byte(dev, W1_F2D_READ_EEPROM);
	w1_write_byte(dev, offset & 0xff);
	w1_write_byte(dev, offset >> 8);

	return w1_read_buf(dev, buf, count);
}

static const struct eeprom_ops ds2431_ops = {
	.read_buf	= ds2431_read_buf,
};

U_BOOT_DRIVER(ds2431) = {
	.name		= "ds2431",
	.id		= UCLASS_EEPROM,
	.ops		= &ds2431_ops,
};

U_BOOT_W1_DEVICE(ds2431, W1_FAMILY_DS2431);
