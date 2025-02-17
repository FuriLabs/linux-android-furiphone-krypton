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

#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <linux/backlight.h>

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
#include "../mediatek/mtk_drm_graphics_base.h"
#include "../mediatek/mtk_log.h"
#include "../mediatek/mtk_panel_ext.h"
#endif

#ifdef CONFIG_MTK_ROUND_CORNER_SUPPORT
#include "../mediatek/mtk_corner_pattern/mtk_data_hw_roundedpattern.h"
#endif
//prize added by wanagyongsheng, lcm support, 20210417-start
#if defined(CONFIG_PRIZE_HARDWARE_INFO)
#include "../../../misc/mediatek/hardware_info/hardware_info.h"
extern struct hardware_info current_lcm_info;
#endif
//prize added by wanagyongsheng, lcm support, 20210417-end

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

#define lcm_dcs_write_seq(ctx, seq...)                                         \
	({                                                                     \
		const u8 d[] = {seq};                                          \
		BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64,                           \
				 "DCS sequence too big for stack");            \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                          \
	})

#define lcm_dcs_write_seq_static(ctx, seq...)                                  \
	({                                                                     \
		static const u8 d[] = {seq};                                   \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                          \
	})

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
		ret = lcm_dcs_read(ctx, 0x0A, buffer, 1);
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
	ret = regulator_set_voltage(disp_bias_pos, 5400000, 5400000);
	if (ret < 0)
		pr_err("set voltage disp_bias_pos fail, ret = %d\n", ret);
	retval |= ret;

	ret = regulator_set_voltage(disp_bias_neg, 5400000, 5400000);
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
        gpiod_set_value(ctx->reset_gpio, 1);
        udelay(1000 * 10);
        gpiod_set_value(ctx->reset_gpio, 0);
        udelay(1000 * 10);
        gpiod_set_value(ctx->reset_gpio, 1);
        udelay(1000*10);
        gpiod_set_value(ctx->reset_gpio, 0);
        udelay(1000 * 10);
        gpiod_set_value(ctx->reset_gpio, 1);
        udelay(1000 * 10);
        gpiod_set_value(ctx->reset_gpio, 0);
        udelay(1000 * 10);
        gpiod_set_value(ctx->reset_gpio, 1);
        udelay(1000 * 10);
        gpiod_set_value(ctx->reset_gpio, 0);
        udelay(1000 * 10);
        gpiod_set_value(ctx->reset_gpio, 1);
        udelay(1000*10);

	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	

//-------------  Display Initial Code Setting  -------------------------
//Stop reload
        lcm_dcs_write_seq_static(ctx,0x41,0x5A);
 
//INT CANCEL
        lcm_dcs_write_seq_static(ctx,0x4C,0x03);
 
//SPI Not LoadFinish
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x24);
        lcm_dcs_write_seq_static(ctx,0x90,0x5A);

//VCOM_以下两行为正扫VCOM，项目烧录VCOM时需屏蔽发客户
        //lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x03);
        //lcm_dcs_write_seq_static(ctx,0x80,0x25,0x01);

//----------------------LCD initial code start----------------------//
//Blank 8
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x08);
        lcm_dcs_write_seq_static(ctx,0x80,0xC8,0x2C,0x11,0x00);
//Blank 9
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x09);
        lcm_dcs_write_seq_static(ctx,0x80,0x00,0x00,0x00,0x00,0x60,0xC5,0x4A,0x01,0x40,0xA0,0x0C,0x82,0x20,0x08,0x30,0x00);
        lcm_dcs_write_seq_static(ctx,0x90,0x99,0x04,0x42,0x0A,0xE3,0x91,0x80,0xF0,0xB9,0xCD,0x6B,0xD8,0x3E,0x21,0x00,0x00);
        lcm_dcs_write_seq_static(ctx,0xA0,0x01,0x55,0x55,0x00,0xA0,0x4D,0x06,0x11,0x0D,0x60,0x5A,0xFF,0xFF,0x03,0xA5,0xE6);
        lcm_dcs_write_seq_static(ctx,0xB0,0x08,0xC9,0x16,0x64,0x0B,0x00,0x00,0x11,0x07,0x60,0x00,0xFF,0xFF,0x03,0xFF,0x34);
        lcm_dcs_write_seq_static(ctx,0xC0,0x0C,0x3F,0x1F,0x9F,0x0F,0x00,0x08,0x00);
//Blank 10
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x0A);
        lcm_dcs_write_seq_static(ctx,0x80,0xFF,0x51,0xA3,0x02,0x00,0x07,0x0D,0x15,0x1F,0x2A,0x2A,0x53,0x00,0x29,0xFF,0xDC);
        lcm_dcs_write_seq_static(ctx,0x90,0xCD,0xA4,0xA1,0x8E,0x79,0x63,0x4B,0x16,0x00,0x07,0x0D,0x15,0x1F,0x2A,0x2A,0x53);
        lcm_dcs_write_seq_static(ctx,0xA0,0x00,0x29,0xFF,0xDC,0xCD,0xA4,0xA1,0x8E,0x79,0x63,0x4B,0x16,0x00);
//Blank 11
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x0B);
        lcm_dcs_write_seq_static(ctx,0x80,0x00,0x00,0x20,0x44,0x08,0x00,0xA0,0x47,0x00,0x00,0x10,0x22,0x04,0x00,0xB0,0x23);
        lcm_dcs_write_seq_static(ctx,0x90,0x15,0x00);
//Blank 12
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x0C);
        lcm_dcs_write_seq_static(ctx,0x80,0xBA,0x68,0x68,0x81,0xAF,0x75,0x40,0x0B,0x00,0x60,0x15,0x00,0x50,0x15,0x56,0x51);
        lcm_dcs_write_seq_static(ctx,0x90,0x15,0x55,0x61,0x15,0x00,0x60,0x15,0x00,0x50,0x15,0x56,0x51,0x15,0x55,0x61,0x95);
        lcm_dcs_write_seq_static(ctx,0xA0,0xAB,0x18,0x00,0x05,0x00,0x05,0x00,0x05,0x00,0x49,0x29,0x84,0x52,0x01,0x09,0x00);
        lcm_dcs_write_seq_static(ctx,0xB0,0x00,0x00);
//Blank 13
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x0D);
        lcm_dcs_write_seq_static(ctx,0x80,0xF0,0xB1,0x71,0xEF,0x4B,0xC0,0x80,0x5A,0x00,0x0E,0x00,0xC0,0x12,0x00,0xAF);
//Blank 14
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x0E);
        lcm_dcs_write_seq_static(ctx,0x80,0xFF,0x00,0x55,0x55,0x23,0x88,0x88,0x08,0x1C);
//Blank 15
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x0F);
        lcm_dcs_write_seq_static(ctx,0x80,0xD1,0x07,0x70,0xC0,0x12,0x20,0xC4,0x08,0x52,0x51,0x58,0x49,0x03,0x52,0x4C,0x4C);
        lcm_dcs_write_seq_static(ctx,0x90,0x68,0x68,0x68,0x4C,0x4C,0x7C,0x10,0x00,0x20,0x06,0xC2,0x00,0x04,0x06,0x0C,0x40);
        lcm_dcs_write_seq_static(ctx,0xA0,0x00,0x92,0x00,0x04);
//Blank 16
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x10);
        lcm_dcs_write_seq_static(ctx,0x80,0x00,0x00,0x03,0xE7,0x1F,0x17,0x20,0x48,0x60,0xD2,0xCF,0x18,0x30,0x88,0x41,0x8A);
        lcm_dcs_write_seq_static(ctx,0x90,0x39,0x28,0xA9,0xC5,0x9A,0x7B,0xF0,0x07,0x7E,0xE0,0x07,0x7E,0x20,0x10,0x00);
//Blank 17
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x11);
        lcm_dcs_write_seq_static(ctx,0x80,0x46,0x77,0x03,0x00,0xCA,0xF3,0xFF,0x83,0x40,0x08,0xC4,0x06,0xA1,0xD8,0x24,0x18);
        lcm_dcs_write_seq_static(ctx,0x90,0x30,0xC6,0x66,0xC1,0x80,0x39,0x15,0xCB,0xE5,0xD2,0x68,0x6C,0x36,0x1D,0x04,0x08);
        lcm_dcs_write_seq_static(ctx,0xA0,0x5A,0xD8,0x88,0x60,0xB0,0x81,0x40,0x1A,0x1B,0x48,0x63,0x03,0xB9,0x00,0x1C,0x80);
        lcm_dcs_write_seq_static(ctx,0xB0,0x50,0x30,0x00,0xE0,0xE1,0x01,0x00,0x28,0x0E,0x06,0x03,0x11,0x51,0x51,0x55,0xCD);
        lcm_dcs_write_seq_static(ctx,0xC0,0x14,0x11,0x91,0x90,0x88,0x10,0x09,0x09,0x88,0xC6,0xE3,0x81,0x00,0x20,0x80,0x21);
        lcm_dcs_write_seq_static(ctx,0xD0,0x42,0x88,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x32,0x04,0x41,0x26,0x01,0x12,0x20);
        lcm_dcs_write_seq_static(ctx,0xE0,0x01,0x92,0x24,0x01,0x92,0x04,0x00,0x00,0x20,0x01,0x92,0x04,0x00,0x85,0x11,0x0C);
        lcm_dcs_write_seq_static(ctx,0xF0,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x5E,0x4A,0x01,0x78,0x00,0x08,0x00,0x08);
//Blank 18
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x12);
        lcm_dcs_write_seq_static(ctx,0x80,0x00,0x00,0x00,0x00,0x00,0x02,0x03,0x00,0x00,0x00,0x00,0x02,0x03,0x00,0x3C,0x37);
        lcm_dcs_write_seq_static(ctx,0x90,0xF1,0xE7,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x08,0x00,0x00,0x00,0x2D,0x23,0x05);
        lcm_dcs_write_seq_static(ctx,0xA0,0xFB,0x2A,0x2D,0x23,0x05,0xFB,0x3F);
//Blank 19
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x13);
        lcm_dcs_write_seq_static(ctx,0x80,0xFD,0x0F,0x00,0x0C,0x00,0x00,0x00,0xA0,0x01,0x2E,0x01,0x1C,0x44,0x0C,0xCE,0xE7);
        lcm_dcs_write_seq_static(ctx,0x90,0x62,0x0E,0x04,0x9B,0xAC,0x21,0x01,0x00,0xD0,0x93,0x24,0x49,0x06,0x20,0x88,0x13);
        lcm_dcs_write_seq_static(ctx,0xA0,0x88,0x13,0x88,0x13,0x9F,0xB8,0x8B,0x58,0x02,0x88,0x13,0x88,0x13,0x58,0x02,0x00);
        lcm_dcs_write_seq_static(ctx,0xB0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1B,0x10);
//Blank 20
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x14);
        lcm_dcs_write_seq_static(ctx,0x80,0x01,0x02,0xC1,0x38,0xE9,0xEF,0xF7,0xFB,0xFD,0x7E,0x01,0x00,0x00,0x7C,0xC5,0xC4);
        lcm_dcs_write_seq_static(ctx,0x90,0xF9,0x4C,0x20,0x1E,0xCE,0x67,0x02,0x5B,0x70,0x3E,0x13,0xD8,0x84,0xF3,0x99,0x00);
        lcm_dcs_write_seq_static(ctx,0xA0,0x00,0x9C,0xCF,0x00,0x00,0xE0,0x7C,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
        lcm_dcs_write_seq_static(ctx,0xB0,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x00,0xD4,0x2F,0xFD,0x2A,0x00,0x40,0xA1);
        lcm_dcs_write_seq_static(ctx,0xC0,0x50,0xB8,0x0B,0xF5,0x33,0x81,0x51,0x1C,0x77,0xA0,0x7E,0x26,0x30,0x8A,0xE3,0x16);
        lcm_dcs_write_seq_static(ctx,0xD0,0xD4,0xCF,0x04,0x46,0x71,0xDC,0x86,0xFA,0x99,0xC0,0x28,0x8E,0x5B,0x51,0x3F,0x13);
        lcm_dcs_write_seq_static(ctx,0xE0,0x18,0xC5,0x71,0x3B,0xEA,0x67,0x02,0xA3,0x38,0x00,0x40,0xFD,0x4C,0x60,0x14,0x07);
        lcm_dcs_write_seq_static(ctx,0xF0,0x00,0xA8,0x9F,0x09,0x8C,0xE2,0x18,0x01,0xBF,0xDF,0x08,0x00,0x00,0x00,0x00,0x00);
//Blank 21
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x15);
        lcm_dcs_write_seq_static(ctx,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x52,0xD9,0xE0,0xF7);
        lcm_dcs_write_seq_static(ctx,0x90,0xA3,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
        lcm_dcs_write_seq_static(ctx,0xA0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
        lcm_dcs_write_seq_static(ctx,0xB0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xC0,0x13,0x00,0xF0,0x00,0x84,0xD6,0x34,0x2D);
        lcm_dcs_write_seq_static(ctx,0xC0,0xAE,0x0B,0x86,0xE2,0x48,0x16,0xA9,0x6D,0xDB,0xB6,0x6D,0xDB,0xB6,0x6D,0xDB,0xB6);
        lcm_dcs_write_seq_static(ctx,0xD0,0x6D,0xDB,0xB6,0x6D,0xDB,0xB6,0x6D,0xDB,0xB6,0x6D,0xDB,0xB6,0x6D,0xDB,0xD6,0x34);
        lcm_dcs_write_seq_static(ctx,0xE0,0x29,0xAE,0x03,0x04,0xC2,0x40,0x14,0xA8,0x6D,0xDB,0xB6,0x6D,0xDB,0xB6,0x6D,0xDB);
        lcm_dcs_write_seq_static(ctx,0xF0,0xB6,0x6D,0xDB,0xB6,0x6D,0xDB,0xB6,0x6D,0xDB,0xB6,0x6D,0xDB,0xB6,0x6D,0x5B,0x80);
//Blank 22
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x16);
        lcm_dcs_write_seq_static(ctx,0x80,0x82,0x82,0x82,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
        lcm_dcs_write_seq_static(ctx,0x90,0x00,0x00,0x00,0x00,0xF0,0x20);
//Blank 24
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x18);
        lcm_dcs_write_seq_static(ctx,0x80,0x04,0xFF,0x00,0x80,0x80,0x00,0x04,0x20,0x00,0x01,0x08,0x40,0x00,0x02,0x10,0x80);
        lcm_dcs_write_seq_static(ctx,0x90,0x00,0x04);
//Blank 25
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x19);
        lcm_dcs_write_seq_static(ctx,0x80,0xF0,0xD9,0xC8,0xBA,0xAF,0xA6,0x9E,0x98,0x92,0x8D,0x88,0x84,0x00,0x60,0xF6,0xCF);
        lcm_dcs_write_seq_static(ctx,0x90,0xFC,0x6F,0xF6,0xEF,0xCF,0xAF,0x0F,0xFF,0xAF,0xB5,0x71,0x0E,0x6C,0x4A,0x69,0x08);
        lcm_dcs_write_seq_static(ctx,0xA0,0x10,0x10,0x62,0x00);
//Blank 26
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x1A);
        lcm_dcs_write_seq_static(ctx,0x80,0x00,0x04,0x08,0x0C,0x00,0x10,0x14,0x18,0x1C,0x00,0x20,0x28,0x30,0x38,0x00,0x40);
        lcm_dcs_write_seq_static(ctx,0x90,0x48,0x50,0x58,0x00,0x60,0x68,0x70,0x78,0x00,0x80,0x88,0x90,0x98,0x00,0xA0,0xA8);
        lcm_dcs_write_seq_static(ctx,0xA0,0xB0,0xB8,0x00,0xC0,0xC8,0xD0,0xD8,0x00,0xE0,0xE8,0xF0,0xF8,0x00,0xFC,0xFE,0xFF);
        lcm_dcs_write_seq_static(ctx,0xB0,0x00,0x00,0x04,0x08,0x0C,0x00,0x10,0x14,0x18,0x1C,0x00,0x20,0x28,0x30,0x38,0x00);
        lcm_dcs_write_seq_static(ctx,0xC0,0x40,0x48,0x50,0x58,0x00,0x60,0x68,0x70,0x78,0x00,0x80,0x88,0x90,0x98,0x00,0xA0);
        lcm_dcs_write_seq_static(ctx,0xD0,0xA8,0xB0,0xB8,0x00,0xC0,0xC8,0xD0,0xD8,0x00,0xE0,0xE8,0xF0,0xF8,0x00,0xFC,0xFE);
        lcm_dcs_write_seq_static(ctx,0xE0,0xFF,0x00,0x00,0x04,0x08,0x0C,0x00,0x10,0x14,0x18,0x1C,0x00,0x20,0x28,0x30,0x38);
        lcm_dcs_write_seq_static(ctx,0xF0,0x00,0x40,0x48,0x50,0x58,0x00,0x60,0x68,0x70,0x78,0x00,0x80,0x88,0x90,0x98,0x00);
//Blank 27
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x1B);
        lcm_dcs_write_seq_static(ctx,0x80,0xA0,0xA8,0xB0,0xB8,0x00,0xC0,0xC8,0xD0,0xD8,0x00,0xE0,0xE8,0xF0,0xF8,0x00,0xFC);
        lcm_dcs_write_seq_static(ctx,0x90,0xFE,0xFF,0x00,0x00);
//Blank 32
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x20);
        lcm_dcs_write_seq_static(ctx,0x80,0x84,0x00,0x02,0x14,0x00,0x00,0x00);
//Blank 34
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x22);
        lcm_dcs_write_seq_static(ctx,0x80,0x2D,0xD3,0x00,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x9F,0x00);
//Blank 35
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x23);
        lcm_dcs_write_seq_static(ctx,0x80,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
        lcm_dcs_write_seq_static(ctx,0x90,0xFF,0x0F,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0xFF,0x07,0x35);
//Blank 36
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x24);
        lcm_dcs_write_seq_static(ctx,0x80,0x00,0x03,0x00,0xFF,0xFF,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x56,0x4E);
        lcm_dcs_write_seq_static(ctx,0x90,0x5A,0x5A,0x5A,0x55,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);

//----------------------LCD initial code End----------------------//

//SPI FINISH
        lcm_dcs_write_seq_static(ctx,0x42,0x24);
        lcm_dcs_write_seq_static(ctx,0x90,0x00);
//Blank select 2F 
        lcm_dcs_write_seq_static(ctx,0x41,0x5A,0x2F);
        lcm_dcs_write_seq_static(ctx,0x19,0x01);

lcm_dcs_write_seq_static(ctx,0x11);
udelay(120);
lcm_dcs_write_seq_static(ctx,0x29);

//CMD1 LEDPWM set
        lcm_dcs_write_seq_static(ctx,0x51,0xff,0x0f);
        lcm_dcs_write_seq_static(ctx,0x53,0x24);
        lcm_dcs_write_seq_static(ctx,0x55,0x00);
 
//-------------  Display Initial Setting end -------------------------

udelay(10);
        printk("icnl9911c %s:%d",__func__,__LINE__);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;

	return 0;
}

static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->prepared)
		return 0;
	
        printk("ft8006s_an %s:%d",__func__,__LINE__);
	lcm_dcs_write_seq_static(ctx,0X28,0X00);
	udelay(10);
	lcm_dcs_write_seq_static(ctx,0X10,0X00);
	udelay(120);
	lcm_dcs_write_seq_static(ctx,0X04,0X5A);
	lcm_dcs_write_seq_static(ctx,0X05,0X5A);
	ctx->error = 0;
	ctx->prepared = false;
#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
	lcm_panel_bias_disable();
#else
        printk("ft8006s_an %s:%d",__func__,__LINE__);
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

#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
	lcm_panel_bias_enable();
#else
        printk("ft8006s_an %s:%d",__func__,__LINE__);
#endif

	lcm_panel_init(ctx);

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

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	return 0;
}

#define HFP (40)
#define HSA (10)
#define HBP (38)
#define VFP (120)
#define VSA (8)
#define VBP (106)
#define VAC (1520)
#define HAC (720)


static struct drm_display_mode default_mode = {
	.clock = 89955,//129939,
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
	unsigned char id[3] = {0x00, 0x80, 0x00};
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
	.pll_clk = 296,
        .ssc_disable = 0,
        .ssc_range = 1,
	.vfp_low_power = 236,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
	},

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

	panel->connector->display_info.width_mm = 65;
	panel->connector->display_info.height_mm = 129;

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
		pr_info("%s+ skip probe due to not current lcm\n", __func__);
		return -ENODEV;
	}

	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO
			 | MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET
			 | MIPI_DSI_CLOCK_NON_CONTINUOUS;

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
//prize added by wanagyongsheng, lcm support, 20210417-start
#if defined(CONFIG_PRIZE_HARDWARE_INFO)
	{
	strcpy(current_lcm_info.chip,"ft8006s-an");
	strcpy(current_lcm_info.vendor,"unknow");
    sprintf(current_lcm_info.id,"0x%04x",0x8000);
    strcpy(current_lcm_info.more,"LCM");
	}
#endif
//prize added by wanagyongsheng, lcm support, 20210417-end
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
	{ .compatible = "ft8006s-an", },
	{ }
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-ft8006s-an",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_DESCRIPTION("FT8006S_AN LCD Panel Driver");
MODULE_LICENSE("GPL v2");
