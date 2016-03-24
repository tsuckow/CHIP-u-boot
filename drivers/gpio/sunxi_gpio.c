/*
 * (C) Copyright 2012 Henrik Nordstrom <henrik@henriknordstrom.net>
 *
 * Based on earlier arch/arm/cpu/armv7/sunxi/gpio.c:
 *
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Tom Cubie <tangliang@allwinnertech.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <dm.h>
#include <errno.h>
#include <fdtdec.h>
#include <malloc.h>
#include <asm/arch/gpio.h>
#include <asm/io.h>
#include <asm/gpio.h>
#include <dm/device-internal.h>
#include <dt-bindings/gpio/gpio.h>

DECLARE_GLOBAL_DATA_PTR;

#define SUNXI_GPIOS_PER_BANK	SUNXI_GPIO_A_NR

static int sunxi_gpio_output(u32 pin, u32 val)
{
	u32 dat;
	u32 bank = GPIO_BANK(pin);
	u32 num = GPIO_NUM(pin);
	struct sunxi_gpio *pio = BANK_TO_GPIO(bank);

	dat = readl(&pio->dat);
	if (val)
		dat |= 0x1 << num;
	else
		dat &= ~(0x1 << num);

	writel(dat, &pio->dat);

	return 0;
}

static int sunxi_gpio_input(u32 pin)
{
	u32 dat;
	u32 bank = GPIO_BANK(pin);
	u32 num = GPIO_NUM(pin);
	struct sunxi_gpio *pio = BANK_TO_GPIO(bank);

	dat = readl(&pio->dat);
	dat >>= num;

	return dat & 0x1;
}

static int sunxi_name_to_gpio_pin(const char *name)
{
	int group = 0;
	int groupsize = 9 * 32;
	long pin;
	char *eptr;

	if (*name == 'P' || *name == 'p')
		name++;
	if (*name >= 'A') {
		group = *name - (*name > 'a' ? 'a' : 'A');
		groupsize = 32;
		name++;
	}

	pin = simple_strtol(name, &eptr, 10);
	if (!*name || *eptr)
		return -1;
	if (pin < 0 || pin > groupsize || group >= 9)
		return -1;
	return group * 32 + pin;
}

#ifndef CONFIG_DM_GPIO
int gpio_request(unsigned gpio, const char *label)
{
	return 0;
}

int gpio_free(unsigned gpio)
{
	return 0;
}

int gpio_direction_input(unsigned gpio)
{
	sunxi_gpio_set_cfgpin(gpio, SUNXI_GPIO_INPUT);

	return 0;
}

int gpio_direction_output(unsigned gpio, int value)
{
	sunxi_gpio_set_cfgpin(gpio, SUNXI_GPIO_OUTPUT);

	return sunxi_gpio_output(gpio, value);
}

int gpio_get_value(unsigned gpio)
{
	return sunxi_gpio_input(gpio);
}

int gpio_set_value(unsigned gpio, int value)
{
	return sunxi_gpio_output(gpio, value);
}

int sunxi_name_to_gpio(const char *name)
{
	return sunxi_name_to_gpio_pin(name);
}

#endif

int sunxi_name_to_gpio_bank(const char *name)
{
	int group = 0;

	if (*name == 'P' || *name == 'p')
		name++;
	if (*name >= 'A') {
		group = *name - (*name > 'a' ? 'a' : 'A');
		return group;
	}

	return -1;
}

#ifdef CONFIG_DM_GPIO
/* TODO(sjg@chromium.org): Remove this function and use device tree */
int sunxi_name_to_gpio(const char *name)
{
	unsigned int gpio;
	int ret;
#if !defined CONFIG_SPL_BUILD && defined CONFIG_AXP_GPIO
	char lookup[8];

	if (strcasecmp(name, "AXP0-VBUS-DETECT") == 0) {
		sprintf(lookup, SUNXI_GPIO_AXP0_PREFIX "%d",
			SUNXI_GPIO_AXP0_VBUS_DETECT);
		name = lookup;
	} else if (strcasecmp(name, "AXP0-VBUS-ENABLE") == 0) {
		sprintf(lookup, SUNXI_GPIO_AXP0_PREFIX "%d",
			SUNXI_GPIO_AXP0_VBUS_ENABLE);
		name = lookup;
	}

#endif
	ret = gpio_lookup_name(name, NULL, NULL, &gpio);
	if (!ret)
		return gpio;

	return sunxi_name_to_gpio_pin(name);
}

static int sunxi_gpio_direction_input(struct udevice *dev, unsigned offset)
{
	sunxi_gpio_set_cfgpin(offset, SUNXI_GPIO_INPUT);

	return 0;
}

static int sunxi_gpio_direction_output(struct udevice *dev, unsigned offset,
				       int value)
{
	sunxi_gpio_set_cfgpin(offset, SUNXI_GPIO_OUTPUT);

	return sunxi_gpio_output(offset, value);
}

static int sunxi_gpio_get_value(struct udevice *dev, unsigned offset)
{
	return sunxi_gpio_input(offset);
}

static int sunxi_gpio_set_value(struct udevice *dev, unsigned offset,
				int value)
{
	return sunxi_gpio_output(offset, value);
}

static int sunxi_gpio_get_function(struct udevice *dev, unsigned offset)
{
	int func;

	func = sunxi_gpio_get_cfgpin(offset);

	if (func == SUNXI_GPIO_OUTPUT)
		return GPIOF_OUTPUT;
	else if (func == SUNXI_GPIO_INPUT)
		return GPIOF_INPUT;
	else
		return GPIOF_FUNC;
}

static int sunxi_gpio_xlate(struct udevice *dev, struct gpio_desc *desc,
			    struct fdtdec_phandle_args *args)
{
	desc->offset = args->args[0] * 32 + args->args[1];
	desc->flags = args->args[2] & GPIO_ACTIVE_LOW ? GPIOD_ACTIVE_LOW : 0;

	return 0;
}

static const struct dm_gpio_ops gpio_sunxi_ops = {
	.direction_input	= sunxi_gpio_direction_input,
	.direction_output	= sunxi_gpio_direction_output,
	.get_value		= sunxi_gpio_get_value,
	.set_value		= sunxi_gpio_set_value,
	.get_function		= sunxi_gpio_get_function,
	.xlate			= sunxi_gpio_xlate,
};

static int gpio_sunxi_probe(struct udevice *dev)
{
	struct gpio_dev_priv *uc_priv = dev_get_uclass_priv(dev);

	if (!fdt_node_check_compatible(gd->fdt_blob, dev->of_offset,
				       "allwinner,sun6i-a31-r-pinctrl"))
		uc_priv->gpio_count = 2 * SUNXI_GPIOS_PER_BANK;
	else if (!fdt_node_check_compatible(gd->fdt_blob, dev->of_offset,
					    "allwinner,sun8i-a23-r-pinctrl"))
		uc_priv->gpio_count = SUNXI_GPIOS_PER_BANK;
	else
		uc_priv->gpio_count = SUNXI_GPIOS_PER_BANK * SUNXI_GPIO_BANKS;

	return 0;
}

static const struct udevice_id sunxi_gpio_ids[] = {
	{ .compatible = "allwinner,sun4i-a10-pinctrl" },
	{ .compatible = "allwinner,sun5i-a10s-pinctrl" },
	{ .compatible = "allwinner,sun5i-a13-pinctrl" },
	{ .compatible = "allwinner,sun6i-a31-pinctrl" },
	{ .compatible = "allwinner,sun6i-a31s-pinctrl" },
	{ .compatible = "allwinner,sun7i-a20-pinctrl" },
	{ .compatible = "allwinner,sun8i-a23-pinctrl" },
	{ .compatible = "allwinner,sun8i-a33-pinctrl" },
	{ .compatible = "allwinner,sun9i-a80-pinctrl" },
	{ .compatible = "allwinner,sun6i-a31-r-pinctrl" },
	{ .compatible = "allwinner,sun8i-a23-r-pinctrl" },
	{ }
};

U_BOOT_DRIVER(gpio_sunxi) = {
	.name	= "gpio_sunxi",
	.id	= UCLASS_GPIO,
	.ops	= &gpio_sunxi_ops,
	.of_match = sunxi_gpio_ids,
	.probe	= gpio_sunxi_probe,
};
#endif
