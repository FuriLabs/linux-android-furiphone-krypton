/*
 * Copyright (c) 2015 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/backlight.h>
#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mtk_panel_ext.h"
#include "../mediatek/mtk_log.h"
#include "../mediatek/mtk_drm_graphics_base.h"
#endif

#ifdef CONFIG_MTK_ROUND_CORNER_SUPPORT
#include "../mediatek/mtk_corner_pattern/mtk_data_hw_roundedpattern.h"
#endif

#if defined(CONFIG_RT4831A_I2C)
#include "../../../misc/mediatek/gate_ic/gate_i2c.h"
#endif

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *bias_pos, *bias_neg;

	bool prepared;
	bool enabled;

	int error;
};

#define lcm_dcs_write_seq(ctx, seq...) \
({\
	const u8 d[] = { seq };\
	BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64, "DCS sequence too big for stack");\
	lcm_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

#define lcm_dcs_write_seq_static(ctx, seq...) \
({\
	static const u8 d[] = { seq };\
	lcm_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

//prize add by lipengpeng 20210420 start 
//extern int fts_reset_proc_on(int delay);
extern int fts_reset_proc(int hdelayms);
extern void fts_enter_low_power(void);
extern int fts_reset_proc_off(int delay);
extern int fts_pinctrl_select_i2c_gpio(int n);
//prize add by lipengpeng 20210420 end 
static inline struct lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
}

static void lcm_dcs_write(struct lcm *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	char *addr;

	if (ctx->error < 0)
		return;

	addr = (char *)data;
	if ((int)*addr < 0xB0)
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	else
		ret = mipi_dsi_generic_write(dsi, data, len);
	if (ret < 0) {
		dev_err(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}

#ifdef PANEL_SUPPORT_READBACK
static int lcm_dcs_read(struct lcm *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error < 0)
		return 0;

	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		dev_err(ctx->dev, "error %d reading dcs seq:(%#x)\n", ret, cmd);
		ctx->error = ret;
	}

	return ret;
}

static void lcm_panel_get_data(struct lcm *ctx)
{
	u8 buffer[3] = {0};
	static int ret;

	if (ret == 0) {
		ret = lcm_dcs_read(ctx,  0x0A, buffer, 1);
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			 ret, buffer[0] | (buffer[1] << 8));
	}
}
#endif

#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
static struct regulator *disp_bias_pos;
static struct regulator *disp_bias_neg;

static int lcm_panel_bias_regulator_init(void)
{
	static int regulator_inited;
	int ret = 0;

	if (regulator_inited)
		return ret;

	/* please only get regulator once in a driver */
	disp_bias_pos = regulator_get(NULL, "dsv_pos");
	if (IS_ERR(disp_bias_pos)) { /* handle return value */
		ret = PTR_ERR(disp_bias_pos);
		pr_err("get dsv_pos fail, error: %d\n", ret);
		return ret;
	}

	disp_bias_neg = regulator_get(NULL, "dsv_neg");
	if (IS_ERR(disp_bias_neg)) { /* handle return value */
		ret = PTR_ERR(disp_bias_neg);
		pr_err("get dsv_neg fail, error: %d\n", ret);
		return ret;
	}

	regulator_inited = 1;
	return ret; /* must be 0 */
}

static int lcm_panel_bias_enable(void)
{
	int ret = 0;
	int retval = 0;

	lcm_panel_bias_regulator_init();

	/* set voltage with min & max*/
	ret = regulator_set_voltage(disp_bias_pos, 5800000, 5800000);
	if (ret < 0)
		pr_err("set voltage disp_bias_pos fail, ret = %d\n", ret);
	retval |= ret;
    mdelay(5);//prize add by lipengpeng 20220223 start 
	ret = regulator_set_voltage(disp_bias_neg, 5800000, 5800000);
	if (ret < 0)
		pr_err("set voltage disp_bias_neg fail, ret = %d\n", ret);
	retval |= ret;

	/* enable regulator */
	ret = regulator_enable(disp_bias_pos);
	if (ret < 0)
		pr_err("enable regulator disp_bias_pos fail, ret = %d\n", ret);
	retval |= ret;

	ret = regulator_enable(disp_bias_neg);
	if (ret < 0)
		pr_err("enable regulator disp_bias_neg fail, ret = %d\n", ret);
	retval |= ret;

	return retval;
}

static int lcm_panel_bias_disable(void)
{
	int ret = 0;
	int retval = 0;

	lcm_panel_bias_regulator_init();

	ret = regulator_disable(disp_bias_neg);
	if (ret < 0)
		pr_err("disable regulator disp_bias_neg fail, ret = %d\n", ret);
	retval |= ret;

	ret = regulator_disable(disp_bias_pos);
	if (ret < 0)
		pr_err("disable regulator disp_bias_pos fail, ret = %d\n", ret);
	retval |= ret;

	return retval;
}
#endif

static void lcm_panel_init(struct lcm *ctx)
{
	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return;
	}
	gpiod_set_value(ctx->reset_gpio, 0);
#if BITS_PER_LONG == 32
	mdelay(15 * 1000);
#else
	udelay(15 * 1000);
#endif
	gpiod_set_value(ctx->reset_gpio, 1);
	udelay(1 * 1000);
	gpiod_set_value(ctx->reset_gpio, 0);
#if BITS_PER_LONG == 32
	mdelay(10 * 1000);
#else
	udelay(10 * 1000);
#endif
	gpiod_set_value(ctx->reset_gpio, 1);
#if BITS_PER_LONG == 32
	mdelay(10 * 1000);
#else
	udelay(10 * 1000);
#endif
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

//----------------------LCD initial code start----------------------//
//CMD2 ENABLE
lcm_dcs_write_seq_static(ctx,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xFF,0x87,0x19,0x01);	
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xFF,0x87,0x19);	
lcm_dcs_write_seq_static(ctx,0x00,0xA1);
lcm_dcs_write_seq_static(ctx,0xB3,0x04,0x38,0x09,0x24);
lcm_dcs_write_seq_static(ctx,0x00,0xA5);
lcm_dcs_write_seq_static(ctx,0xB3,0xC0);
lcm_dcs_write_seq_static(ctx,0x00,0xA6);
lcm_dcs_write_seq_static(ctx,0xB3,0xF8);
lcm_dcs_write_seq_static(ctx,0x00,0x85);
lcm_dcs_write_seq_static(ctx,0xA7,0x03);
lcm_dcs_write_seq_static(ctx,0x00,0xD0);
lcm_dcs_write_seq_static(ctx,0xC3,0x46);
lcm_dcs_write_seq_static(ctx,0x00,0xD3);
lcm_dcs_write_seq_static(ctx,0xC3,0x30);
lcm_dcs_write_seq_static(ctx,0x00,0xD4);
lcm_dcs_write_seq_static(ctx,0xC3,0x46);
lcm_dcs_write_seq_static(ctx,0x00,0xD7);
lcm_dcs_write_seq_static(ctx,0xC3,0x30);
lcm_dcs_write_seq_static(ctx,0x00,0xCA);
lcm_dcs_write_seq_static(ctx,0xC0,0x80);
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xc2,0x83,0x01,0x01,0x81,0x82,0x01,0x01,0x81,0x82,0x00,0x01,0x81,0x81,0x00,0x01,0x81);
lcm_dcs_write_seq_static(ctx,0x00,0x90);
lcm_dcs_write_seq_static(ctx,0xc2,0x80,0x00,0x01,0x81,0x01,0x00,0x01,0x81,0x02,0x00,0x01,0x81,0x03,0x00,0x01,0x81);
lcm_dcs_write_seq_static(ctx,0x00,0xa0);
lcm_dcs_write_seq_static(ctx,0xc2,0x8a,0x0a,0x00,0x01,0x8a,0x89,0x0a,0x00,0x01,0x8a,0x88,0x0a,0x00,0x01,0x8a);
lcm_dcs_write_seq_static(ctx,0x00,0xb0);
lcm_dcs_write_seq_static(ctx,0xc2,0x87,0x0a,0x00,0x01,0x8A,0x00,0x00,0x00,0x01,0x81,0x00,0x00,0x00,0x01,0x81);
lcm_dcs_write_seq_static(ctx,0x00,0xc0);
lcm_dcs_write_seq_static(ctx,0xc2,0x00,0x00,0x00,0x01,0x81,0x00,0x00,0x00,0x01,0x81,0x00,0x00,0x00,0x01,0x81);
lcm_dcs_write_seq_static(ctx,0x00,0xd0);
lcm_dcs_write_seq_static(ctx,0xc2,0x00,0x00,0x00,0x01,0x81,0x00,0x00,0x00,0x01,0x81,0x00,0x00,0x00,0x01,0x81);
lcm_dcs_write_seq_static(ctx,0x00,0xe0);
lcm_dcs_write_seq_static(ctx,0xc2,0x33,0x33,0x33,0x33,0x33,0x33,0x00,0x00,0x12,0x00,0x0a,0x70,0x01,0x81);
lcm_dcs_write_seq_static(ctx,0x00,0xf0);
lcm_dcs_write_seq_static(ctx,0xc2,0x80,0x00,0x50,0x05,0x11,0x80,0x00,0x50,0x05,0x11,0xff,0xff,0xff,0x01);
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xcb,0xfd,0xfd,0xfc,0x01,0xfd,0x01,0x01,0xfd,0xFE,0x02,0xfd,0x01,0xfd,0xfd,0x01,0x01);
lcm_dcs_write_seq_static(ctx,0x00,0x90);
lcm_dcs_write_seq_static(ctx,0xcb,0xff,0x00,0x00,0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0xa0);
lcm_dcs_write_seq_static(ctx,0xcb,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0xb0);
lcm_dcs_write_seq_static(ctx,0xcb,0x51,0x55,0xA5,0x55);
lcm_dcs_write_seq_static(ctx,0x00,0xe0);
lcm_dcs_write_seq_static(ctx,0xc3,0x35);	
lcm_dcs_write_seq_static(ctx,0x00,0xe4);
lcm_dcs_write_seq_static(ctx,0xc3,0x35);
lcm_dcs_write_seq_static(ctx,0x00,0xe8);
lcm_dcs_write_seq_static(ctx,0xc3,0x35);
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xcc,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x26,0x29,0x24,0x04,0x04,0x18,0x17,0x16);
lcm_dcs_write_seq_static(ctx,0x00,0x90);
lcm_dcs_write_seq_static(ctx,0xcc,0x07,0x09,0x01,0x25,0x25,0x22,0x24,0x03);
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xcd,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x26,0x29,0x24,0x04,0x04,0x18,0x17,0x16);
lcm_dcs_write_seq_static(ctx,0x00,0x90);
lcm_dcs_write_seq_static(ctx,0xcd,0x06,0x08,0x01,0x25,0x25,0x22,0x24,0x02);
lcm_dcs_write_seq_static(ctx,0x00,0xa0);
lcm_dcs_write_seq_static(ctx,0xcc,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x26,0x29,0x24,0x04,0x04,0x18,0x17,0x16);
lcm_dcs_write_seq_static(ctx,0x00,0xb0);
lcm_dcs_write_seq_static(ctx,0xcc,0x08,0x06,0x01,0x25,0x25,0x24,0x23,0x02);
lcm_dcs_write_seq_static(ctx,0x00,0xa0);
lcm_dcs_write_seq_static(ctx,0xcd,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x26,0x29,0x24,0x04,0x04,0x18,0x17,0x16);
lcm_dcs_write_seq_static(ctx,0x00,0xb0);
lcm_dcs_write_seq_static(ctx,0xcd,0x09,0x07,0x01,0x25,0x25,0x24,0x23,0x03);
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xC0,0x00,0x7B,0x00,0x6B,0x00,0x10);
lcm_dcs_write_seq_static(ctx,0x00,0x87);
lcm_dcs_write_seq_static(ctx,0xC0,0x01,0x01);
lcm_dcs_write_seq_static(ctx,0x00,0x8A);
lcm_dcs_write_seq_static(ctx,0xC0,0x1D,0x08);
lcm_dcs_write_seq_static(ctx,0x00,0x90);
lcm_dcs_write_seq_static(ctx,0xC0,0x00,0x7B,0x00,0x6B,0x00,0x10);
lcm_dcs_write_seq_static(ctx,0x00,0xA0);
lcm_dcs_write_seq_static(ctx,0xC0,0x01,0x0A,0x00,0x3A,0x00,0x10);
lcm_dcs_write_seq_static(ctx,0x00,0xD7);
lcm_dcs_write_seq_static(ctx,0xC0,0x00,0x78,0x00,0x6E,0x00,0x10);
lcm_dcs_write_seq_static(ctx,0x00,0xB0);
lcm_dcs_write_seq_static(ctx,0xC0,0x00,0x7B,0x01,0xFA,0x10);
lcm_dcs_write_seq_static(ctx,0x00,0xC1);
lcm_dcs_write_seq_static(ctx,0xC0,0x00,0xB4,0x00,0x8C,0x00,0x78,0x00,0xD2);
lcm_dcs_write_seq_static(ctx,0x00,0xA5);
lcm_dcs_write_seq_static(ctx,0xC1,0x00,0x28,0x00,0x02);
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xCE,0x01,0x80,0x01,0x09,0x00,0xD8,0x00,0xD8,0x00,0x90,0x00,0x90,0x0D,0x0E,0x09);
lcm_dcs_write_seq_static(ctx,0x00,0x90);
lcm_dcs_write_seq_static(ctx,0xCE,0x00,0x82,0x0D,0x5C,0x00,0x82,0x80,0x09,0x00,0x04);
lcm_dcs_write_seq_static(ctx,0x00,0xA0);
lcm_dcs_write_seq_static(ctx,0xCE,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0xB0);
lcm_dcs_write_seq_static(ctx,0xCE,0x11,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0xC0);
lcm_dcs_write_seq_static(ctx,0xCE,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0xD0);
lcm_dcs_write_seq_static(ctx,0xCE,0x91,0x00,0x0A,0x01,0x01,0x00,0x5C,0x01);
lcm_dcs_write_seq_static(ctx,0x00,0xE0);
lcm_dcs_write_seq_static(ctx,0xCE,0x88,0x08,0x02,0x15,0x02,0x15,0x02,0x15,0x00,0x2B,0x00,0x5F);
lcm_dcs_write_seq_static(ctx,0x00,0xF0);
lcm_dcs_write_seq_static(ctx,0xCE,0x80,0x16,0x0B,0x0F,0x01,0x00,0x00,0xFE,0x01,0x0A);
lcm_dcs_write_seq_static(ctx,0x00,0x82);
lcm_dcs_write_seq_static(ctx,0xCF,0x06);
lcm_dcs_write_seq_static(ctx,0x00,0x84);
lcm_dcs_write_seq_static(ctx,0xCF,0x06);
lcm_dcs_write_seq_static(ctx,0x00,0x87);
lcm_dcs_write_seq_static(ctx,0xCF,0x06);
lcm_dcs_write_seq_static(ctx,0x00,0x89);
lcm_dcs_write_seq_static(ctx,0xCF,0x06);
lcm_dcs_write_seq_static(ctx,0x00,0x8A);
lcm_dcs_write_seq_static(ctx,0xCF,0x07);
lcm_dcs_write_seq_static(ctx,0x00,0x8B);
lcm_dcs_write_seq_static(ctx,0xCF,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0x8C);
lcm_dcs_write_seq_static(ctx,0xCF,0x06);
lcm_dcs_write_seq_static(ctx,0x00,0x92);
lcm_dcs_write_seq_static(ctx,0xCF,0x06);
lcm_dcs_write_seq_static(ctx,0x00,0x94);
lcm_dcs_write_seq_static(ctx,0xCF,0x06);
lcm_dcs_write_seq_static(ctx,0x00,0x97);
lcm_dcs_write_seq_static(ctx,0xCF,0x06);
lcm_dcs_write_seq_static(ctx,0x00,0x99);
lcm_dcs_write_seq_static(ctx,0xCF,0x06);
lcm_dcs_write_seq_static(ctx,0x00,0x9A);
lcm_dcs_write_seq_static(ctx,0xCF,0x07);
lcm_dcs_write_seq_static(ctx,0x00,0x9B);
lcm_dcs_write_seq_static(ctx,0xCF,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0x9C);
lcm_dcs_write_seq_static(ctx,0xCF,0x06);
lcm_dcs_write_seq_static(ctx,0x00,0xA0);
lcm_dcs_write_seq_static(ctx,0xCF,0x24);
lcm_dcs_write_seq_static(ctx,0x00,0xA2);
lcm_dcs_write_seq_static(ctx,0xCF,0x06);
lcm_dcs_write_seq_static(ctx,0x00,0xA4);
lcm_dcs_write_seq_static(ctx,0xCF,0x06);
lcm_dcs_write_seq_static(ctx,0x00,0xA7);
lcm_dcs_write_seq_static(ctx,0xCF,0x06);
lcm_dcs_write_seq_static(ctx,0x00,0xA9);
lcm_dcs_write_seq_static(ctx,0xCF,0x06);
lcm_dcs_write_seq_static(ctx,0x00,0xB0);
lcm_dcs_write_seq_static(ctx,0xCF,0x00,0x00,0x6C,0x70,0x00,0x04,0x04,0xA4,0xA8);
lcm_dcs_write_seq_static(ctx,0x00,0xC0);
lcm_dcs_write_seq_static(ctx,0xCF,0x08,0x08,0xCA,0xCE,0x00,0x00,0x00,0x08,0x0C);
lcm_dcs_write_seq_static(ctx,0x00,0x83);
lcm_dcs_write_seq_static(ctx,0xA4,0x22);
lcm_dcs_write_seq_static(ctx,0x00,0x87);
lcm_dcs_write_seq_static(ctx,0xA4,0x0F);
lcm_dcs_write_seq_static(ctx,0x00,0x89);
lcm_dcs_write_seq_static(ctx,0xA4,0x0F);
lcm_dcs_write_seq_static(ctx,0x00,0x8D);
lcm_dcs_write_seq_static(ctx,0xA4,0x0F);
lcm_dcs_write_seq_static(ctx,0x00,0x8F);
lcm_dcs_write_seq_static(ctx,0xA4,0x0F);
lcm_dcs_write_seq_static(ctx,0x00,0x85);
lcm_dcs_write_seq_static(ctx,0xA7,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0xE8);
lcm_dcs_write_seq_static(ctx,0xC0,0x40);
lcm_dcs_write_seq_static(ctx,0x00,0x85);
lcm_dcs_write_seq_static(ctx,0xC4,0x1E);
lcm_dcs_write_seq_static(ctx,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE1,0x00,0x03,0x05,0x10,0x36,0x1d,0x25,0x2b,0x35,0x0a,0x3d,0x44,0x4a,0x4f,0xe7,0x54,0x5c,0x64,0x6b,0x60,0x72,0x79,0x80,0x89,0x0d,0x93,0x99,0x9f,0xa7,0x8c,0xb0,0xba,0xc8,0xd2,0x3c,0xdb,0xeb,0xf7,0xff,0x6b);
lcm_dcs_write_seq_static(ctx,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE2,0x00,0x03,0x05,0x10,0x34,0x17,0x1f,0x25,0x2f,0x4a,0x37,0x3e,0x44,0x49,0xe7,0x4e,0x56,0x5e,0x65,0x60,0x6c,0x73,0x7a,0x83,0x0d,0x8d,0x93,0x99,0xa1,0x8c,0xaa,0xb4,0xc2,0xcc,0x3c,0xda,0xeb,0xf7,0xff,0xeb);
lcm_dcs_write_seq_static(ctx,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE3,0x00,0x03,0x05,0x10,0x36,0x1d,0x25,0x2b,0x35,0x0a,0x3d,0x44,0x4a,0x4f,0xe7,0x54,0x5c,0x64,0x6b,0x60,0x72,0x79,0x80,0x89,0x0d,0x93,0x99,0x9f,0xa7,0x8c,0xb0,0xba,0xc8,0xd2,0x3c,0xdb,0xeb,0xf7,0xff,0x6b);
lcm_dcs_write_seq_static(ctx,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE4,0x00,0x03,0x05,0x10,0x34,0x17,0x1f,0x25,0x2f,0x4a,0x37,0x3e,0x44,0x49,0xe7,0x4e,0x56,0x5e,0x65,0x60,0x6c,0x73,0x7a,0x83,0x0d,0x8d,0x93,0x99,0xa1,0x8c,0xaa,0xb4,0xc2,0xcc,0x3c,0xda,0xeb,0xf7,0xff,0xeb);
lcm_dcs_write_seq_static(ctx,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE5,0x00,0x03,0x05,0x10,0x36,0x1d,0x25,0x2b,0x35,0x0a,0x3d,0x44,0x4a,0x4f,0xe7,0x54,0x5c,0x64,0x6b,0x60,0x72,0x79,0x80,0x89,0x0d,0x93,0x99,0x9f,0xa7,0x8c,0xb0,0xba,0xc8,0xd2,0x3c,0xdb,0xeb,0xf7,0xff,0x6b);
lcm_dcs_write_seq_static(ctx,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE6,0x00,0x03,0x05,0x10,0x34,0x17,0x1f,0x25,0x2f,0x4a,0x37,0x3e,0x44,0x49,0xe7,0x4e,0x56,0x5e,0x65,0x60,0x6c,0x73,0x7a,0x83,0x0d,0x8d,0x93,0x99,0xa1,0x8c,0xaa,0xb4,0xc2,0xcc,0x3c,0xda,0xeb,0xf7,0xff,0xeb);
lcm_dcs_write_seq_static(ctx,0x00,0x82);
lcm_dcs_write_seq_static(ctx,0xC5,0x50,0x50);
lcm_dcs_write_seq_static(ctx,0x00,0x84);
lcm_dcs_write_seq_static(ctx,0xC5,0x32,0x32,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0x87);
lcm_dcs_write_seq_static(ctx,0xC5,0x60,0x0C);
lcm_dcs_write_seq_static(ctx,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xD8,0x27,0x2F);
//lcm_dcs_write_seq_static(ctx,0x00,0x00);
//lcm_dcs_write_seq_static(ctx,0xD9,0x00,0x91,0x91);
lcm_dcs_write_seq_static(ctx,0x00,0xA3);
lcm_dcs_write_seq_static(ctx,0xC5,0x19);
lcm_dcs_write_seq_static(ctx,0x00,0xA9);
lcm_dcs_write_seq_static(ctx,0xC5,0x23);
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xa4,0x70);
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xC5,0x86,0x86);
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xa7,0x03); 
lcm_dcs_write_seq_static(ctx,0x00,0x8c);
lcm_dcs_write_seq_static(ctx,0xC3,0x03,0x00,0x30);
lcm_dcs_write_seq_static(ctx,0x00,0xB0);
lcm_dcs_write_seq_static(ctx,0xF5,0x00);
lcm_dcs_write_seq_static(ctx,0x00,0x82);
lcm_dcs_write_seq_static(ctx,0xa4,0x29,0x23);
lcm_dcs_write_seq_static(ctx,0x00,0xC1);
lcm_dcs_write_seq_static(ctx,0xB6,0x09,0x89,0x68);
lcm_dcs_write_seq_static(ctx,0x00,0x80);
lcm_dcs_write_seq_static(ctx,0xB4,0x0A);
lcm_dcs_write_seq_static(ctx,0x00,0xB0);
lcm_dcs_write_seq_static(ctx,0xF3,0x02,0xFD);
//----------------------LCD initial code End----------------------//

lcm_dcs_write_seq_static(ctx,0x11,0);
msleep(120);
lcm_dcs_write_seq_static(ctx,0x29,0);
lcm_dcs_write_seq_static(ctx,0x35,0x01);
msleep(60);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
		
	if (!ctx->enabled)
		return 0;
	pr_info("%s\n", __func__);
	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}
	pr_info("%s_end\n", __func__);
	ctx->enabled = false;

	return 0;
}

static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	pr_info("%s\n", __func__);
	if (!ctx->prepared)
		return 0;

	fts_enter_low_power();
	msleep(50);
	lcm_dcs_write_seq_static(ctx, 0x28);
	msleep(60);
	lcm_dcs_write_seq_static(ctx, 0x10);
	msleep(220);
	lcm_dcs_write_seq_static(ctx, 0x00,0x00);
	lcm_dcs_write_seq_static(ctx, 0xFF,0x87,0x19,0x01);
	lcm_dcs_write_seq_static(ctx, 0x00,0x80);
	lcm_dcs_write_seq_static(ctx, 0xFF,0x87,0x19);
	lcm_dcs_write_seq_static(ctx, 0x00,0x00);
	lcm_dcs_write_seq_static(ctx, 0xF7,0x5A,0xA5,0x95,0x27);
	msleep(80);//add by lipengpeng 20210419 LCD FAE requirements
	
		
	ctx->error = 0;
	ctx->prepared = false;
	
#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
	lcm_panel_bias_disable();
#elif defined(CONFIG_RT4831A_I2C)
	/*this is rt4831a*/
	_gate_ic_i2c_panel_bias_enable(0);
	_gate_ic_Power_off();
#else

	msleep(10);//100

	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, 1);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);


	ctx->bias_neg = devm_gpiod_get_index(ctx->dev,
		"bias", 1, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_neg)) {
		dev_err(ctx->dev, "%s: cannot get bias_neg %ld\n",
			__func__, PTR_ERR(ctx->bias_neg));
		return PTR_ERR(ctx->bias_neg);
	}
	gpiod_set_value(ctx->bias_neg, 0);
	devm_gpiod_put(ctx->dev, ctx->bias_neg);

	udelay(1000);

	ctx->bias_pos = devm_gpiod_get_index(ctx->dev,
		"bias", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_pos)) {
		dev_err(ctx->dev, "%s: cannot get bias_pos %ld\n",
			__func__, PTR_ERR(ctx->bias_pos));
		return PTR_ERR(ctx->bias_pos);
	}
	gpiod_set_value(ctx->bias_pos, 0);
	devm_gpiod_put(ctx->dev, ctx->bias_pos);
#endif
	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	pr_info("%s\n", __func__);
	if (ctx->prepared)
		return 0;
printk("ft8719 resume start 1111\n");
fts_reset_proc(120);

#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
	lcm_panel_bias_enable();
	printk("ft8719 resume start 22\n");
#elif defined(CONFIG_RT4831A_I2C)
	_gate_ic_Power_on();
	/*rt4831a co-work with leds_i2c*/
	_gate_ic_i2c_panel_bias_enable(1);
#else
	ctx->bias_pos = devm_gpiod_get_index(ctx->dev,
		"bias", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_pos)) {
		dev_err(ctx->dev, "%s: cannot get bias_pos %ld\n",
			__func__, PTR_ERR(ctx->bias_pos));
		return PTR_ERR(ctx->bias_pos);
	}
	gpiod_set_value(ctx->bias_pos, 1);
	devm_gpiod_put(ctx->dev, ctx->bias_pos);
printk("ft8719 resume start 333\n");
	udelay(2000);

	ctx->bias_neg = devm_gpiod_get_index(ctx->dev,
		"bias", 1, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_neg)) {
		dev_err(ctx->dev, "%s: cannot get bias_neg %ld\n",
			__func__, PTR_ERR(ctx->bias_neg));
		return PTR_ERR(ctx->bias_neg);
	}
	gpiod_set_value(ctx->bias_neg, 1);
	devm_gpiod_put(ctx->dev, ctx->bias_neg);
#endif
printk("ft8719 resume start 444\n");
	lcm_panel_init(ctx);
printk("ft8719 resume start 555\n");
	ret = ctx->error;
	if (ret < 0)
		lcm_unprepare(panel);

	ctx->prepared = true;

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_rst(panel);
#endif
#ifdef PANEL_SUPPORT_READBACK
	lcm_panel_get_data(ctx);
#endif

	return ret;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	pr_info("%s\n", __func__);
	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}
	pr_info("%s_end\n", __func__);
	ctx->enabled = true;

	return 0;
}

#define HFP (25)
#define HSA (4)
#define HBP (16)
#define VFP (112)
#define VSA (4)
#define VBP (12)
#define VAC (2340)
#define HAC (1080)
static  struct drm_display_mode default_mode = {
	.clock = 165000,     //htotal*vtotal*vrefresh/1000   1116*2468*60/1000=165257
	.hdisplay = HAC,
	.hsync_start = HAC + HFP,
	.hsync_end = HAC + HFP + HSA,
	.htotal = HAC + HFP + HSA + HBP,
	.vdisplay = VAC,
	.vsync_start = VAC + VFP,
	.vsync_end = VAC + VFP + VSA,
	.vtotal = VAC + VFP + VSA + VBP,
	.vrefresh = 60,
};

#if defined(CONFIG_MTK_PANEL_EXT)
static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);

	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	return 0;
}

static int panel_ata_check(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	unsigned char data[3] = {0x00, 0x00, 0x00};
	unsigned char id[3] = {0x00, 0x00, 0x00};
	ssize_t ret;

	ret = mipi_dsi_dcs_read(dsi, 0x4, data, 3);
	if (ret < 0) {
		pr_err("%s error\n", __func__);
		return 0;
	}

	DDPINFO("ATA read data %x %x %x\n", data[0], data[1], data[2]);

	if (data[0] == id[0] &&
			data[1] == id[1] &&
			data[2] == id[2])
		return 1;

	DDPINFO("ATA expect read data is %x %x %x\n",
			id[0], id[1], id[2]);

	return 0;
}

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb, void *handle,
				 unsigned int level)
{
	char bl_tb0[] = {0x51, 0xFF};

	bl_tb0[1] = level;

	if (!cb)
		return -1;

	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));

	return 0;
}

static int lcm_get_virtual_heigh(void)
{
	return VAC;
}

static int lcm_get_virtual_width(void)
{
	return HAC;
}
static struct mtk_panel_params ext_params = {
	.pll_clk = 540,//mipi clock  165257
	.vfp_low_power = 112,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9C,
	},
//prize add by lipengpeng 20220112 start 
   .lcm_esd_check_table[1] = {
		.cmd = 0xAC,
		.count = 1,
		.para_list[0] = 0x20,
	},
//prize add by lipengpeng 20220112 end 
	//.dyn = {
	//	.switch_en = 1,
	//	.pll_clk = 540,
	//	.hfp = 16,
	//	.vfp = 112,
	//},
};

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ata_check = panel_ata_check,
	.get_virtual_heigh = lcm_get_virtual_heigh,
	.get_virtual_width = lcm_get_virtual_width,
};
#endif

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int num_modes;

	unsigned int bpc;

	struct {
		unsigned int width;
		unsigned int height;
	} size;

	struct {
		unsigned int prepare;
		unsigned int enable;
		unsigned int disable;
		unsigned int unprepare;
	} delay;
};

static int lcm_get_modes(struct drm_panel *panel)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		dev_err(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(panel->connector, mode);
//prize add by lipengpeng 20220208 start 
	panel->connector->display_info.width_mm = 67;
	panel->connector->display_info.height_mm = 145;//sqrt((size*25.4)^2/(2340^2+1080^2))*2340
//prize add by lipengpeng 20220208 end 
	return 1;
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct lcm *ctx;
	struct device_node *backlight;
	int ret;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;

	dsi_node = of_get_parent(dev->of_node);
	if (dsi_node) {
		endpoint = of_graph_get_next_endpoint(dsi_node, NULL);
		if (endpoint) {
			remote_node = of_graph_get_remote_port_parent(endpoint);
			if (!remote_node) {
				pr_info("No panel connected,skip probe lcm\n");
				return -ENODEV;
			}
			pr_info("device node name:%s\n", remote_node->name);
		}
	}
	if (remote_node != dev->of_node) {
		pr_info("%s+ skip probe due to not current lcm, remote:%pOF , dev:%s\n", __func__, remote_node, dev_name(dev));
		return -ENODEV;
	}

	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
//prize add by lipengpeng 20220112 start 
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE
			 | MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET
			 | MIPI_DSI_CLOCK_NON_CONTINUOUS;
//prize add by lipengpeng 20220112 end 
	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "%s: cannot get reset-gpios %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	devm_gpiod_put(dev, ctx->reset_gpio);
#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
	lcm_panel_bias_enable();
#else
	ctx->bias_pos = devm_gpiod_get_index(dev, "bias", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_pos)) {
		dev_err(dev, "%s: cannot get bias-pos 0 %ld\n",
			__func__, PTR_ERR(ctx->bias_pos));
		return PTR_ERR(ctx->bias_pos);
	}
	devm_gpiod_put(dev, ctx->bias_pos);

	ctx->bias_neg = devm_gpiod_get_index(dev, "bias", 1, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_neg)) {
		dev_err(dev, "%s: cannot get bias-neg 1 %ld\n",
			__func__, PTR_ERR(ctx->bias_neg));
		return PTR_ERR(ctx->bias_neg);
	}
	devm_gpiod_put(dev, ctx->bias_neg);
#endif
	ctx->prepared = true;
	ctx->enabled = true;

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &lcm_drm_funcs;

	ret = drm_panel_add(&ctx->panel);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_handle_reg(&ctx->panel);
	ret = mtk_panel_ext_create(dev, &ext_params, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;
#endif

	pr_info("%s-\n", __func__);
	return ret;
}

static int lcm_remove(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id lcm_of_match[] = {
	{ .compatible = "cfgd,ft8719,jdi", },
	{ }
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-cfgd-ft8719-jdi",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("Yi-Lun Wang <Yi-Lun.Wang@mediatek.com>");
MODULE_DESCRIPTION("truly td4330 CMD LCD Panel Driver");
MODULE_LICENSE("GPL v2");
