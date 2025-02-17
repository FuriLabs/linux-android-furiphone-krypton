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

//prize add by lvyuanchuan for lcd hardware info 20220331 start
#if defined(CONFIG_PRIZE_HARDWARE_INFO)
#include "../../../misc/mediatek/hardware_info/hardware_info.h"
extern struct hardware_info current_lcm_info;
#endif
//prize add by lvyuanchuan for lcd hardware info 20220331 end
struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	//prize add by lipengpeng 20211026  start
	struct gpio_desc *pm_enable_gpio;
	//prize add by lipengpeng 20211026  start
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

//prize add by lipengpeng 20211026  start
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
//prize add by lipengpeng 20211026  start
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
//prize add by lipengpeng 20211026  start	
#if BITS_PER_LONG == 32
	mdelay(15 * 1000);
#else
//prize add by lipengpeng 20211026  end
	udelay(15 * 1000);
//prize add by lipengpeng 20211026  start
#endif
//prize add by lipengpeng 20211026  end
	gpiod_set_value(ctx->reset_gpio, 1);
	udelay(1 * 1000);
	gpiod_set_value(ctx->reset_gpio, 0);
//prize add by lipengpeng 20211026  start
#if BITS_PER_LONG == 32
	mdelay(10 * 1000);
#else
//prize add by lipengpeng 20211026  end
	udelay(10 * 1000);
//prize add by lipengpeng 20211026  start
#endif
//prize add by lipengpeng 20211026  end
	gpiod_set_value(ctx->reset_gpio, 1);
//prize add by lipengpeng 20211026  start
#if BITS_PER_LONG == 32
	mdelay(10 * 1000);
#else
	udelay(10 * 1000);
#endif
//prize add by lipengpeng 20211026  end
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

lcm_dcs_write_seq_static(ctx,0xFF,0x98,0x81,0x01);
lcm_dcs_write_seq_static(ctx,0x00,0x46);  
lcm_dcs_write_seq_static(ctx,0x01,0x16);
lcm_dcs_write_seq_static(ctx,0x02,0x10);    //10
lcm_dcs_write_seq_static(ctx,0x03,0x10);
lcm_dcs_write_seq_static(ctx,0x08,0x80);  
lcm_dcs_write_seq_static(ctx,0x09,0x12);   //DUMMT CK
lcm_dcs_write_seq_static(ctx,0x0a,0x71);
lcm_dcs_write_seq_static(ctx,0x0b,0x00);  //clk keep 10 off 00
lcm_dcs_write_seq_static(ctx,0x14,0x8A);   //KEEP
lcm_dcs_write_seq_static(ctx,0x15,0x8A);   //KEEP
lcm_dcs_write_seq_static(ctx,0x0c,0x10);     //01
lcm_dcs_write_seq_static(ctx,0x0d,0x10);    //01
lcm_dcs_write_seq_static(ctx,0x0e,0x00);  
lcm_dcs_write_seq_static(ctx,0x0f,0x00);
lcm_dcs_write_seq_static(ctx,0x10,0x01);
lcm_dcs_write_seq_static(ctx,0x11,0x01);
lcm_dcs_write_seq_static(ctx,0x12,0x01);
lcm_dcs_write_seq_static(ctx,0x24,0x00);  //01 
lcm_dcs_write_seq_static(ctx,0x25,0x09);  
lcm_dcs_write_seq_static(ctx,0x26,0x10);
lcm_dcs_write_seq_static(ctx,0x27,0x10);
lcm_dcs_write_seq_static(ctx,0x31,0x21);  //STV_C
lcm_dcs_write_seq_static(ctx,0x32,0x07);  
lcm_dcs_write_seq_static(ctx,0x33,0x01);  
lcm_dcs_write_seq_static(ctx,0x34,0x00);  
lcm_dcs_write_seq_static(ctx,0x35,0x02);  
lcm_dcs_write_seq_static(ctx,0x36,0x07);
lcm_dcs_write_seq_static(ctx,0x37,0x07);
lcm_dcs_write_seq_static(ctx,0x38,0x07); 
lcm_dcs_write_seq_static(ctx,0x39,0x07);  
lcm_dcs_write_seq_static(ctx,0x3a,0x17);  //CLK8
lcm_dcs_write_seq_static(ctx,0x3b,0x15);  //CLK6
lcm_dcs_write_seq_static(ctx,0x3c,0x07);  
lcm_dcs_write_seq_static(ctx,0x3d,0x07);  
lcm_dcs_write_seq_static(ctx,0x3e,0x13);  //CLK4
lcm_dcs_write_seq_static(ctx,0x3f,0x11);  //CLK2
lcm_dcs_write_seq_static(ctx,0x40,0x09);   //STV_A  
lcm_dcs_write_seq_static(ctx,0x41,0x07); 
lcm_dcs_write_seq_static(ctx,0x42,0x07); 
lcm_dcs_write_seq_static(ctx,0x43,0x07);  
lcm_dcs_write_seq_static(ctx,0x44,0x07);   
lcm_dcs_write_seq_static(ctx,0x45,0x07);   
lcm_dcs_write_seq_static(ctx,0x46,0x07); 
lcm_dcs_write_seq_static(ctx,0x47,0x20);   //STV_C
lcm_dcs_write_seq_static(ctx,0x48,0x07);   
lcm_dcs_write_seq_static(ctx,0x49,0x01);   
lcm_dcs_write_seq_static(ctx,0x4a,0x00); 
lcm_dcs_write_seq_static(ctx,0x4b,0x02); 
lcm_dcs_write_seq_static(ctx,0x4c,0x07); 
lcm_dcs_write_seq_static(ctx,0x4d,0x07);   
lcm_dcs_write_seq_static(ctx,0x4e,0x07); 
lcm_dcs_write_seq_static(ctx,0x4f,0x07); 
lcm_dcs_write_seq_static(ctx,0x50,0x16);   //CLK7
lcm_dcs_write_seq_static(ctx,0x51,0x14);   //CLK5
lcm_dcs_write_seq_static(ctx,0x52,0x07); 
lcm_dcs_write_seq_static(ctx,0x53,0x07);   
lcm_dcs_write_seq_static(ctx,0x54,0x12);   //CLK3
lcm_dcs_write_seq_static(ctx,0x55,0x10);   //CLK1
lcm_dcs_write_seq_static(ctx,0x56,0x08);   //STV_A
lcm_dcs_write_seq_static(ctx,0x57,0x07); 
lcm_dcs_write_seq_static(ctx,0x58,0x07);   
lcm_dcs_write_seq_static(ctx,0x59,0x07);   
lcm_dcs_write_seq_static(ctx,0x5a,0x07); 
lcm_dcs_write_seq_static(ctx,0x5b,0x07); 
lcm_dcs_write_seq_static(ctx,0x5c,0x07); 
lcm_dcs_write_seq_static(ctx,0x61,0x08);   
lcm_dcs_write_seq_static(ctx,0x62,0x07); 
lcm_dcs_write_seq_static(ctx,0x63,0x01); 
lcm_dcs_write_seq_static(ctx,0x64,0x00);   
lcm_dcs_write_seq_static(ctx,0x65,0x02);   
lcm_dcs_write_seq_static(ctx,0x66,0x07); 
lcm_dcs_write_seq_static(ctx,0x67,0x07); 
lcm_dcs_write_seq_static(ctx,0x68,0x07);   
lcm_dcs_write_seq_static(ctx,0x69,0x07);   
lcm_dcs_write_seq_static(ctx,0x6a,0x10); 
lcm_dcs_write_seq_static(ctx,0x6b,0x12); 
lcm_dcs_write_seq_static(ctx,0x6c,0x07);   
lcm_dcs_write_seq_static(ctx,0x6d,0x07);   
lcm_dcs_write_seq_static(ctx,0x6e,0x14); 
lcm_dcs_write_seq_static(ctx,0x6f,0x16); 
lcm_dcs_write_seq_static(ctx,0x70,0x20);   
lcm_dcs_write_seq_static(ctx,0x71,0x07);   
lcm_dcs_write_seq_static(ctx,0x72,0x07); 
lcm_dcs_write_seq_static(ctx,0x73,0x07); 
lcm_dcs_write_seq_static(ctx,0x74,0x07);   
lcm_dcs_write_seq_static(ctx,0x75,0x07);   
lcm_dcs_write_seq_static(ctx,0x76,0x07); 
lcm_dcs_write_seq_static(ctx,0x77,0x09); 
lcm_dcs_write_seq_static(ctx,0x78,0x07);   
lcm_dcs_write_seq_static(ctx,0x79,0x01);   
lcm_dcs_write_seq_static(ctx,0x7a,0x00); 
lcm_dcs_write_seq_static(ctx,0x7b,0x02); 
lcm_dcs_write_seq_static(ctx,0x7c,0x07); 
lcm_dcs_write_seq_static(ctx,0x7d,0x07);   
lcm_dcs_write_seq_static(ctx,0x7e,0x07); 
lcm_dcs_write_seq_static(ctx,0x7f,0x07); 
lcm_dcs_write_seq_static(ctx,0x80,0x11);   
lcm_dcs_write_seq_static(ctx,0x81,0x13); 
lcm_dcs_write_seq_static(ctx,0x82,0x07); 
lcm_dcs_write_seq_static(ctx,0x83,0x07);   
lcm_dcs_write_seq_static(ctx,0x84,0x15); 
lcm_dcs_write_seq_static(ctx,0x85,0x17); 
lcm_dcs_write_seq_static(ctx,0x86,0x21); 
lcm_dcs_write_seq_static(ctx,0x87,0x07); 
lcm_dcs_write_seq_static(ctx,0x88,0x07);   
lcm_dcs_write_seq_static(ctx,0x89,0x07);   
lcm_dcs_write_seq_static(ctx,0x8a,0x07); 
lcm_dcs_write_seq_static(ctx,0x8b,0x07); 
lcm_dcs_write_seq_static(ctx,0x8c,0x07); 
lcm_dcs_write_seq_static(ctx,0xA0,0x01); 
lcm_dcs_write_seq_static(ctx,0xA1,0x10);   
lcm_dcs_write_seq_static(ctx,0xA2,0x08);   
lcm_dcs_write_seq_static(ctx,0xA5,0x10);   
lcm_dcs_write_seq_static(ctx,0xA6,0x10); 
lcm_dcs_write_seq_static(ctx,0xA7,0x00); 
lcm_dcs_write_seq_static(ctx,0xA8,0x00);   
lcm_dcs_write_seq_static(ctx,0xA9,0x09);   
lcm_dcs_write_seq_static(ctx,0xAa,0x09); 
lcm_dcs_write_seq_static(ctx,0xb9,0x40); 
lcm_dcs_write_seq_static(ctx,0xd0,0x01); 
lcm_dcs_write_seq_static(ctx,0xd1,0x00); 
lcm_dcs_write_seq_static(ctx,0xdc,0x35); 
lcm_dcs_write_seq_static(ctx,0xdd,0x42); 
lcm_dcs_write_seq_static(ctx,0xe2,0x00); 
lcm_dcs_write_seq_static(ctx,0xe6,0x22); 
lcm_dcs_write_seq_static(ctx,0xe7,0x54);               //V-porch SRC=V0
lcm_dcs_write_seq_static(ctx,0xFF,0x98,0x81,0x05); 
lcm_dcs_write_seq_static(ctx,0x03,0x00);  //VCOM
lcm_dcs_write_seq_static(ctx,0x04,0xe0);  //FC //VCOM  -1.4v
lcm_dcs_write_seq_static(ctx,0x58,0x62); 
lcm_dcs_write_seq_static(ctx,0x63,0x88);  //GVDDN -5.24v
lcm_dcs_write_seq_static(ctx,0x64,0x8A);  //GVDDP +5.2v
lcm_dcs_write_seq_static(ctx,0x68,0xAA);  //VGHO  15v WAVEFORM
lcm_dcs_write_seq_static(ctx,0x69,0xB1);  //VGH  16V 
lcm_dcs_write_seq_static(ctx,0x6A,0x86);  ////VGLO  -10v   WAVEFORM
lcm_dcs_write_seq_static(ctx,0x6B,0x78);  //VGL=-11V
lcm_dcs_write_seq_static(ctx,0xFF,0x98,0x81,0x06); 
lcm_dcs_write_seq_static(ctx,0x0F,0x40); 
lcm_dcs_write_seq_static(ctx,0x11,0x03); 
lcm_dcs_write_seq_static(ctx,0x13,0x54);   //+0.25%//40c1
lcm_dcs_write_seq_static(ctx,0x14,0x41); 
lcm_dcs_write_seq_static(ctx,0x15,0x01);   //-0.25%//406e
lcm_dcs_write_seq_static(ctx,0x16,0x41); 
lcm_dcs_write_seq_static(ctx,0x17,0xFF); 
lcm_dcs_write_seq_static(ctx,0x18,0x00); 
lcm_dcs_write_seq_static(ctx,0x48,0x0F);  //1 bit ESD
lcm_dcs_write_seq_static(ctx,0x4D,0x80);  //1 bit ESD
lcm_dcs_write_seq_static(ctx,0x4E,0x40);  //1 bit ESD
lcm_dcs_write_seq_static(ctx,0xFF,0x98,0x81,0x08); 
lcm_dcs_write_seq_static(ctx,0xE0,0x00,0x24,0x7A,0xB0,0xF0,0x55,0x23,0x4A,0x76,0x98,0xA5,0xCF,0xFA,0x20,0x44,0xAA,0x6B,0x9C,0xBD,0xE6,0xFF,0x08,0x37,0x6E,0x9C,0x03,0xFF); 
lcm_dcs_write_seq_static(ctx,0xE1,0x00,0x24,0x7A,0xB0,0xF0,0x55,0x23,0x4A,0x76,0x98,0xA5,0xCF,0xFA,0x20,0x44,0xAA,0x6B,0x9C,0xBD,0xE6,0xFF,0x08,0x37,0x6E,0x9C,0x03,0xFF); 
lcm_dcs_write_seq_static(ctx,0xFF,0x98,0x81,0x06); 
lcm_dcs_write_seq_static(ctx,0xD6,0x85);  //FTE=TSVD1,0x FTE1=TSHD
lcm_dcs_write_seq_static(ctx,0x27,0x20);  //VFP 
lcm_dcs_write_seq_static(ctx,0x28,0x20);  //VBP
lcm_dcs_write_seq_static(ctx,0x2E,0x01);  //NL enable
lcm_dcs_write_seq_static(ctx,0xC0,0xF7); 
lcm_dcs_write_seq_static(ctx,0xC1,0x02); 
lcm_dcs_write_seq_static(ctx,0xC2,0x04); 
lcm_dcs_write_seq_static(ctx,0xFF,0x98,0x81,0x0E); 
lcm_dcs_write_seq_static(ctx,0x00,0xA0);  //LV mode
lcm_dcs_write_seq_static(ctx,0x01,0x28);  //DELY_VID
lcm_dcs_write_seq_static(ctx,0x11,0x90); 
lcm_dcs_write_seq_static(ctx,0x13,0x14);  
lcm_dcs_write_seq_static(ctx,0xFF,0x98,0x81,0x02); 
lcm_dcs_write_seq_static(ctx,0x40,0x43); 
lcm_dcs_write_seq_static(ctx,0x42,0x00); 
lcm_dcs_write_seq_static(ctx,0x4A,0x08); 
lcm_dcs_write_seq_static(ctx,0x4D,0x4E); 
lcm_dcs_write_seq_static(ctx,0x4E,0x00); 
lcm_dcs_write_seq_static(ctx,0x1A,0x48);  //pump clk 1H
lcm_dcs_write_seq_static(ctx,0xFF,0x98,0x81,0x07); 
lcm_dcs_write_seq_static(ctx,0x0F,0x02);  //TP_term_GVDD_on         //0525
lcm_dcs_write_seq_static(ctx,0xFF,0x98,0x81,0x00); //Page0
lcm_dcs_write_seq_static(ctx,0x35,0x00);  //TE enable
lcm_dcs_write_seq_static(ctx,0x36,0x00); 
lcm_dcs_write_seq_static(ctx,0x11,0x00);
	msleep(120);
lcm_dcs_write_seq_static(ctx,0x29,0x00);
	msleep(1);
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

	lcm_dcs_write_seq_static(ctx, 0x28);
	msleep(50);
	lcm_dcs_write_seq_static(ctx, 0x10);
	msleep(150);

	ctx->error = 0;
	ctx->prepared = false;
//prize add by lipengpeng 20211026  start
#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
	lcm_panel_bias_disable();
#elif defined(CONFIG_RT4831A_I2C)
	/*this is rt4831a*/
	_gate_ic_i2c_panel_bias_enable(0);
	_gate_ic_Power_off();
#else
//prize add by lipengpeng 20211026  end
	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
//prize add by lipengpeng 20211026  start
//#if defined(CONFIG_PRIZE_LCD_BIAS)
//	display_bias_disable();
//#else
//prize add by lipengpeng 20211026  start
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

//prize add by lipengpeng 20211026  start
#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
	lcm_panel_bias_enable();
#elif defined(CONFIG_RT4831A_I2C)
	_gate_ic_Power_on();
	/*rt4831a co-work with leds_i2c*/
	_gate_ic_i2c_panel_bias_enable(1);

//#if defined(CONFIG_PRIZE_LCD_BIAS)
//	display_bias_enable();
//prize add by lipengpeng 20211026  end 
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

#define HFP (60)
#define HSA (60)
#define HBP (8)
#define VFP (246)
#define VSA (8)
#define VBP (4)
#define VAC (1520)//1920
#define HAC (720)//1080
static const struct drm_display_mode default_mode = {
	.clock = 240000,
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
	unsigned char id[3] = {0x40, 0x00, 0x00};
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

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb,
	void *handle, unsigned int level)
{
	char bl_tb0[] = {0x51, 0xFF};

	bl_tb0[1] = level;

	if (!cb)
		return -1;

	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));

	return 0;
}
//prize add by lipengpeng 20211026  start
static int lcm_get_virtual_heigh(void)
{
	return VAC;
}

static int lcm_get_virtual_width(void)
{
	return HAC;
}
//prize add by lipengpeng 20211026  end 
static struct mtk_panel_params ext_params = {
	.pll_clk = 240,
	.vfp_low_power = 810,
	.cust_esd_check = 0,
	.esd_check_enable = 0,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x1c,
	},
	.dyn = {
		.switch_en = 1,
		.pll_clk = 535,
		.hfp = 32,
		.vfp = 93,
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

	panel->connector->display_info.width_mm = 64;
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
//prize add by lvyuanchuan for lcd hardware info 20220331 start
#if defined(CONFIG_PRIZE_HARDWARE_INFO)
    strcpy(current_lcm_info.chip,"ili9881h");
    strcpy(current_lcm_info.vendor,"cmi");
    sprintf(current_lcm_info.id,"0x%02x",0x02);
    strcpy(current_lcm_info.more,"720X1520");
#endif
//prize add by lvyuanchuan for lcd hardware info 20220331 end
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
	{ .compatible = "cmi,ili9881h,vdo", },
	{ }
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-ili9881h-hdplus-dsi-vdo-cmi-hf",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("Yi-Lun Wang <Yi-Lun.Wang@mediatek.com>");
MODULE_DESCRIPTION("ili9881h vdo LCD Panel Driver");
MODULE_LICENSE("GPL v2");
