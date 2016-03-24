/*
 * Copyright (c) 2015 Free Electrons
 * Copyright (c) 2015 NextThing Co.
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <dm.h>
#include <eeprom.h>

#include <dm/device-internal.h>

int eeprom_read_buf(struct udevice *dev, unsigned offset,
		    u8 *buf, unsigned count)
{
	const struct eeprom_ops *ops = device_get_ops(dev);

	if (!ops->read_buf)
		return -ENOSYS;

	return ops->read_buf(dev, offset, buf, count);
}


UCLASS_DRIVER(eeprom) = {
	.name		= "eeprom",
	.id		= UCLASS_EEPROM,
};

int eeprom_dm_init(void)
{
	struct udevice *bus;
	struct uclass *uc;
	int ret;

	ret = uclass_get(UCLASS_EEPROM, &uc);
	if (ret)
		return ret;

	uclass_foreach_dev(bus, uc) {
		ret = device_probe(bus);
		if (ret == -ENODEV) {	/* No such device. */
			printf("EEPROM not available.\n");
			continue;
		}

		if (ret) {		/* Other error. */
			printf("EEPROM probe failed, error %d\n", ret);
			continue;
		}
	}

	return 0;
}
