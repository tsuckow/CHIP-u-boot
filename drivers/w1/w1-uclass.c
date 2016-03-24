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
#include <w1.h>

#include <dm/device-internal.h>

#define W1_MATCH_ROM	0x55
#define W1_SKIP_ROM	0xcc
#define W1_SEARCH	0xf0

struct w1_bus {
	u64	search_id;
};

struct w1_device {
	u64	id;
};

static int w1_new_device(struct udevice *bus, u64 id)
{
	struct w1_driver_entry *start, *entry;
	int n_ents, ret = -ENODEV;
	u8 family = id & 0xff;

	debug("%s: Detected new device 0x%llx (family 0x%x)\n",
	      bus->name, id, family);

	start = ll_entry_start(struct w1_driver_entry, w1_driver_entry);
	n_ents = ll_entry_count(struct w1_driver_entry, w1_driver_entry);

	for (entry = start; entry != start + n_ents; entry++) {
		const struct driver *drv;
		struct w1_device *w1;
		struct udevice *dev;

		if (entry->family != family)
			continue;

		drv = entry->driver;
		ret = device_bind(bus, drv, drv->name, NULL, -1,
				  &dev);
		if (ret)
			goto error;

		debug("%s: Match found: %s\n", __func__, drv->name);

		w1 = dev_get_parent_platdata(dev);
		w1->id = id;

		return 0;
	}

error:
	debug("%s: No matches found: error %d\n", __func__, ret);
	return ret;
}

static int w1_enumerate(struct udevice *bus)
{
	const struct w1_ops *ops = device_get_ops(bus);
	struct w1_bus *w1 = dev_get_uclass_priv(bus);
	u64 last_rn, rn = w1->search_id, tmp64;
	bool last_device = false;
	int search_bit, desc_bit = 64;
	int last_zero = -1;
	u8 triplet_ret = 0;
	int i;

	if (!ops->reset || !ops->write_byte || !ops->triplet)
		return -ENOSYS;

	while ( !last_device ) {
		last_rn = rn;
		rn = 0;

		/*
		 * Reset bus and all 1-wire device state machines
		 * so they can respond to our requests.
		 *
		 * Return 0 - device(s) present, 1 - no devices present.
		 */
		if (ops->reset(bus)) {
			debug("%s: No devices present on the wire.\n",
			      __func__);
			break;
		}

		/* Start the search */
		ops->write_byte(bus, W1_SEARCH);
		for (i = 0; i < 64; ++i) {
			/* Determine the direction/search bit */
			if (i == desc_bit)
				search_bit = 1;	  /* took the 0 path last time, so take the 1 path */
			else if (i > desc_bit)
				search_bit = 0;	  /* take the 0 path on the next branch */
			else
				search_bit = ((last_rn >> i) & 0x1);

			/* Read two bits and write one bit */
			triplet_ret = ops->triplet(bus, search_bit);

			/* quit if no device responded */
			if ( (triplet_ret & 0x03) == 0x03 )
				break;

			/* If both directions were valid, and we took the 0 path... */
			if (triplet_ret == 0)
				last_zero = i;

			/* extract the direction taken & update the device number */
			tmp64 = (triplet_ret >> 2);
			rn |= (tmp64 << i);
		}

		if ( (triplet_ret & 0x03) != 0x03 ) {
			if ((desc_bit == last_zero) || (last_zero < 0)) {
				last_device = 1;
				w1->search_id = 0;
			} else {
				w1->search_id = rn;
			}
			desc_bit = last_zero;

			w1_new_device(bus, rn);
		}
	}

	return 0;
}

int w1_get_bus(int busnum, struct udevice **busp)
{
	int ret;

	ret = uclass_get_device_by_seq(UCLASS_W1, busnum, busp);
	if (ret) {
		debug("Cannot find w1 bus %d\n", busnum);
		return ret;
	}

	return 0;
}

u8 w1_get_device_family(struct udevice *dev)
{
	struct w1_device *w1 = dev_get_parent_platdata(dev);

	return w1->id & 0xff;
}

int w1_reset_select(struct udevice *dev)
{
	struct w1_device *w1 = dev_get_parent_platdata(dev);
	struct udevice *bus = dev_get_parent(dev);
	const struct w1_ops *ops = device_get_ops(bus);
	int i;

	if (!ops->reset || !ops->write_byte)
		return -ENOSYS;

	ops->reset(bus);

	ops->write_byte(bus, W1_MATCH_ROM);

	for (i = 0; i < sizeof(w1->id); i++)
		ops->write_byte(bus, (w1->id >> (i * 8)) & 0xff);

	return 0;
}

int w1_read_byte(struct udevice *dev)
{
	struct udevice *bus = dev_get_parent(dev);
	const struct w1_ops *ops = device_get_ops(bus);

	if (!ops->read_byte)
		return -ENOSYS;

	return ops->read_byte(bus);
}

int w1_read_buf(struct udevice *dev, u8 *buf, unsigned count)
{
	int i;

	for (i = 0; i < count; i++) {
		int ret = w1_read_byte(dev);
		if (ret < 0)
			return ret;

		buf[i] = ret & 0xff;
	}

	return 0;
}

int w1_write_byte(struct udevice *dev, u8 byte)
{
	struct udevice *bus = dev_get_parent(dev);
	const struct w1_ops *ops = device_get_ops(bus);

	if (!ops->write_byte)
		return -ENOSYS;

	ops->write_byte(bus, byte);

	return 0;
}

static int w1_post_probe(struct udevice *bus)
{
	w1_enumerate(bus);

	return 0;
}

UCLASS_DRIVER(w1) = {
	.name		= "w1",
	.id		= UCLASS_W1,
	.flags		= DM_UC_FLAG_SEQ_ALIAS,
	.per_device_auto_alloc_size	= sizeof(struct w1_bus),
	.per_child_platdata_auto_alloc_size	= sizeof(struct w1_device),
	.post_probe	= w1_post_probe,
};

int w1_init(void)
{
	struct udevice *bus;
	struct uclass *uc;
	int ret;

	ret = uclass_get(UCLASS_W1, &uc);
	if (ret)
		return ret;

	uclass_foreach_dev(bus, uc) {
		ret = device_probe(bus);
		if (ret == -ENODEV) {	/* No such device. */
			printf("W1 controller not available.\n");
			continue;
		}

		if (ret) {		/* Other error. */
			printf("W1 controller probe failed, error %d\n", ret);
			continue;
		}
	}

	return 0;
}
