/*
 * ocp8178_bl.c - ocp8178 backlight driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>

struct ocp8178_backlight {
	struct device *dev;
	struct device *fbdev;

	struct gpio_desc *gpiod;
	unsigned int def_value;
	unsigned int current_value;
};

#define OCP8178_DETECT_DELAY_US		200
#define OCP8178_DETECT_TIME_US		500
#define OCP8178_DETECT_WINDOW_TIME_US	1000
#define OCP8178_START_TIME_US		10
#define OCP8178_END_TIME_US		10
#define OCP8178_SHUTDOWN_TIME_MS	3
#define OCP8178_LOW_BIT_HIGH_TIME_US	10
#define OCP8178_LOW_BIT_LOW_TIME_US	50
#define OCP8178_HIGH_BIT_HIGH_TIME_US	50
#define OCP8178_HIGH_BIT_LOW_TIME_US	10
#define OCP8178_MAX_BRIGHTNESS		9
#define OCP8178_DEFAULT_BRIGHTNESS	5
#define OCP8178_DEVICE_ADDR		0x72

static void ocp8178_enter_1wire_mode(struct ocp8178_backlight *gbl)
{
	unsigned long flags;

	local_irq_save(flags);
	gpiod_set_value(gbl->gpiod, 0);
	mdelay(OCP8178_SHUTDOWN_TIME_MS);
	gpiod_set_value(gbl->gpiod, 1);
	udelay(OCP8178_DETECT_DELAY_US);
	gpiod_set_value(gbl->gpiod, 0);
	udelay(OCP8178_DETECT_TIME_US);
	gpiod_set_value(gbl->gpiod, 1);
	udelay(OCP8178_DETECT_WINDOW_TIME_US);
	local_irq_restore(flags);
}

static inline void write_bit(struct ocp8178_backlight *gbl, int bit)
{
	if (bit) {
		gpiod_set_value(gbl->gpiod, 0);
		udelay(OCP8178_HIGH_BIT_LOW_TIME_US);
		gpiod_set_value(gbl->gpiod, 1);
		udelay(OCP8178_HIGH_BIT_HIGH_TIME_US);
	} else {
		gpiod_set_value(gbl->gpiod, 0);
		udelay(OCP8178_LOW_BIT_LOW_TIME_US);
		gpiod_set_value(gbl->gpiod, 1);
		udelay(OCP8178_LOW_BIT_HIGH_TIME_US);
	}
}

static void ocp8178_write_byte(struct ocp8178_backlight *gbl, u8 byte)
{
	unsigned long flags;
	u8 data = OCP8178_DEVICE_ADDR;
	int i;

	local_irq_save(flags);

	gpiod_set_value(gbl->gpiod, 1);
	udelay(OCP8178_START_TIME_US);
	for (i = 0; i < 8; i++) {
		if (data & 0x80)
			write_bit(gbl, 1);
		else
			write_bit(gbl, 0);
		data <<= 1;
	}
	gpiod_set_value(gbl->gpiod, 0);
	udelay(OCP8178_END_TIME_US);

	data = byte & 0x1f;

	gpiod_set_value(gbl->gpiod, 1);
	udelay(OCP8178_START_TIME_US);
	for (i = 0; i < 8; i++) {
		if (data & 0x80)
			write_bit(gbl, 1);
		else
			write_bit(gbl, 0);
		data <<= 1;
	}
	gpiod_set_value(gbl->gpiod, 0);
	udelay(OCP8178_END_TIME_US);
	gpiod_set_value(gbl->gpiod, 1);

	local_irq_restore(flags);
}

static const u8 ocp8178_bl_table[OCP8178_MAX_BRIGHTNESS + 1] = {
	0, 1, 4, 8, 12, 16, 20, 24, 28, 31
};

static int ocp8178_update_status(struct backlight_device *bl)
{
	struct ocp8178_backlight *gbl = bl_get_data(bl);
	int brightness = bl->props.brightness, i;

	if (bl->props.power != FB_BLANK_UNBLANK ||
	    bl->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK))
		brightness = 0;

	brightness = clamp_t(int, brightness, 0, OCP8178_MAX_BRIGHTNESS);

	for (i = 0; i < 2; i++) {
		ocp8178_enter_1wire_mode(gbl);
		ocp8178_write_byte(gbl, ocp8178_bl_table[brightness]);
	}
	gbl->current_value = brightness;

	return 0;
}

static int ocp8178_get_brightness(struct backlight_device *bl)
{
	struct ocp8178_backlight *gbl = bl_get_data(bl);
	return gbl->current_value;
}

static bool ocp8178_controls_device(struct backlight_device *bl,
				    struct device *display_dev)
{
	struct ocp8178_backlight *gbl = bl_get_data(bl);
	return !gbl->fbdev || gbl->fbdev == display_dev;
}

static const struct backlight_ops ocp8178_backlight_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = ocp8178_update_status,
	.get_brightness = ocp8178_get_brightness,
	.controls_device = ocp8178_controls_device,
};

static int ocp8178_probe_dt(struct platform_device *pdev,
			    struct ocp8178_backlight *gbl)
{
	struct device *dev = &pdev->dev;
	enum gpiod_flags flags;
	int ret = 0;
	u32 value32 = OCP8178_DEFAULT_BRIGHTNESS;

	device_property_read_u32(dev, "default-brightness", &value32);
	gbl->def_value = min_t(u32, value32, OCP8178_MAX_BRIGHTNESS);
	flags = gbl->def_value ? GPIOD_OUT_HIGH : GPIOD_OUT_LOW;

	gbl->gpiod = devm_gpiod_get(dev, "backlight-control", flags);
	if (IS_ERR(gbl->gpiod)) {
		ret = PTR_ERR(gbl->gpiod);

		if (ret != -EPROBE_DEFER) {
			dev_err(dev,
				"Error: The gpios parameter is missing or invalid.\n");
		}
	}

	return ret;
}

static int ocp8178_probe(struct platform_device *pdev)
{
	struct backlight_properties props;
	struct backlight_device *bl;
	struct ocp8178_backlight *gbl;
	int ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	gbl = devm_kzalloc(&pdev->dev, sizeof(*gbl), GFP_KERNEL);
	if (!gbl)
		return -ENOMEM;

	gbl->dev = &pdev->dev;

	ret = ocp8178_probe_dt(pdev, gbl);
	if (ret)
		return ret;

	gbl->current_value = gbl->def_value;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = OCP8178_MAX_BRIGHTNESS;
	bl = devm_backlight_device_register(&pdev->dev, dev_name(&pdev->dev),
					    &pdev->dev, gbl,
					    &ocp8178_backlight_ops, &props);
	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		return PTR_ERR(bl);
	}

	bl->props.brightness = gbl->def_value;
	backlight_update_status(bl);

	platform_set_drvdata(pdev, bl);

	return 0;
}

static struct of_device_id ocp8178_of_match[] = {
	{ .compatible = "ocp8178-backlight" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, ocp8178_of_match);

static struct platform_driver ocp8178_driver = {
	.driver		= {
		.name		= "ocp8178-backlight",
		.of_match_table = of_match_ptr(ocp8178_of_match),
	},
	.probe		= ocp8178_probe,
};

module_platform_driver(ocp8178_driver);

MODULE_DESCRIPTION("OCP8178 Driver");
MODULE_LICENSE("GPL");
