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
	
	lcm_dcs_write_seq_static(ctx,0XF0,0X5A,0X59);
	lcm_dcs_write_seq_static(ctx,0XF1,0XA5,0XA6);
	lcm_dcs_write_seq_static(ctx,0XB0,0X87,0X86,0X85,0X84,0X02,0X03,0X04,0X05,0X33,0X33,0X33,0X33,0X00,0X00,0X00,0X78,0X00,0X00,0X0F,0X05,0X04,0X03,0X02,0X01,0X02,0X03,0X04,0X00,0X00,0X00);
	//lcm_dcs_write_seq_static(ctx,0XB1,0X53,0X43,0X85,0X80,0X00,0X00,0X00,0X7F,0X00,0X00,0X04,0X08,0X54,0X00,0X00,0X00,0X44,0X40,0X02,0X01,0X40,0X02,0X01,0X40,0X02,0X01,0X40,0X02,0X01);//OE=1.95US
	lcm_dcs_write_seq_static(ctx,0XB1,0X53,0X43,0X85,0X80,0X00,0X00,0X00,0X7E,0X00,0X00,0X04,0X08,0X54,0X00,0X00,0X00,0X44,0X40,0X02,0X01,0X40,0X02,0X01,0X40,0X02,0X01,0X40,0X02,0X01);//OE=2.01US
	lcm_dcs_write_seq_static(ctx,0XB2,0X54,0XC4,0X82,0X05,0X40,0X02,0X01,0X40,0X02,0X01,0X05,0X05,0X54,0X0C,0X0C,0X0D,0X0B);
	lcm_dcs_write_seq_static(ctx,0XB3,0X02,0X0C,0X06,0X0C,0X06,0X26,0X26,0X91,0XA2,0X33,0X44,0X00,0X26,0X00,0X18,0X01,0X02,0X08,0X20,0X30,0X08,0X09,0X44,0X20,0X40,0X20,0X40,0X08,0X09,0X22,0X33);
	lcm_dcs_write_seq_static(ctx,0XB4,0X00,0X00,0X00,0X00,0X04,0X06,0X01,0X01,0X10,0X12,0X0C,0X0E,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0XFF,0XFF,0XFC,0X60,0X30,0X00);
	lcm_dcs_write_seq_static(ctx,0XB5,0X00,0X00,0X00,0X00,0X05,0X07,0X01,0X01,0X11,0X13,0X0D,0X0F,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0XFF,0XFF,0XFC,0X60,0X30,0X00);
	lcm_dcs_write_seq_static(ctx,0XB8,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00);
	//lcm_dcs_write_seq_static(ctx,0XBA,0XA4,0XA4);
	lcm_dcs_write_seq_static(ctx,0XBB,0X01,0X05,0X09,0X11,0X0D,0X19,0X1D,0X55,0X25,0X69,0X00,0X21,0X25);
	lcm_dcs_write_seq_static(ctx,0XBC,0X00,0X00,0X00,0X00,0X02,0X20,0XFF,0X00,0X03,0X33,0X01,0X73,0X33,0X00);
	lcm_dcs_write_seq_static(ctx,0XBD,0XE9,0X02,0X4F,0XCF,0X72,0XA4,0X08,0X44,0XAE,0X15);
	lcm_dcs_write_seq_static(ctx,0XBE,0X7D,0X7D,0X46,0X5A,0X0C,0X77,0X43,0X07,0X0E,0X0E);//OP=5.5VGH=14VGL=-14
	lcm_dcs_write_seq_static(ctx,0XBF,0X07,0X25,0X07,0X25,0X7F,0X00,0X11,0X04);
	lcm_dcs_write_seq_static(ctx,0XC0,0X10,0XFF,0XFF,0XFF,0XFF,0XFF,0X00,0XFF,0X00);
	lcm_dcs_write_seq_static(ctx,0XC1,0XC0,0X0C,0X20,0X96,0X04,0X30,0X30,0X04,0X2A,0XF0,0X35,0X00,0X07,0XCF,0XFF,0XFF,0XC0,0X00,0XC0);
	lcm_dcs_write_seq_static(ctx,0XC2,0X00);
	lcm_dcs_write_seq_static(ctx,0XC3,0X06,0X00,0XFF,0X00,0XFF,0X00,0X00,0X81,0X01,0X00,0X00);
	lcm_dcs_write_seq_static(ctx,0XC4,0X84,0X01,0X2B,0X41,0X00,0X3C,0X00,0X03,0X03,0X2E);
	lcm_dcs_write_seq_static(ctx,0XC5,0X03,0X1C,0XB8,0XB8,0X30,0X10,0X42,0X44,0X08,0X09,0X14);
	lcm_dcs_write_seq_static(ctx,0XC6,0X87,0X9B,0X2A,0X29,0X29,0X33,0X64,0X34,0X08,0X04);
	lcm_dcs_write_seq_static(ctx,0XC7,0XF7,0XDE,0XCA,0XB9,0X9A,0X7D,0X4F,0XA2,0X66,0X32,0XFF,0XC5,0X12,0XE1,0XBF,0X91,0X76,0X4F,0X1A,0X7F,0XE4,0X00);
	lcm_dcs_write_seq_static(ctx,0XC8,0XF7,0XDE,0XCA,0XB9,0X9A,0X7D,0X4F,0XA2,0X66,0X32,0XFF,0XC5,0X12,0XE1,0XBF,0X91,0X76,0X4F,0X1A,0X7F,0XE4,0X00);
	lcm_dcs_write_seq_static(ctx,0XCB,0X00);
	lcm_dcs_write_seq_static(ctx,0XD0,0X80,0X0D,0XFF,0X0F,0X61);
	lcm_dcs_write_seq_static(ctx,0XD2,0X42);
	lcm_dcs_write_seq_static(ctx,0XFE,0XFF,0XFF,0XFF,0X40);
	//lcm_dcs_write_seq_static(ctx,0XE0,0X30,0X00,0X00,0X08,0X11,0X3F,0X22,0X62,0XDF,0XA0,0X04,0XCC,0X01,0XFF,0XF6,0XFF,0XF0,0XFD,0XFF,0XFD,0XF8,0XF5,0XFC,0XFC,0XFD,0XFF);
	//lcm_dcs_write_seq_static(ctx,0XE1,0XEF,0XFE,0XFE,0XFE,0XFE,0XEE,0XF0,0X20,0X33,0XFF,0X00,0X00,0X6A,0X90,0XC0,0X0D,0X6A,0XF0,0X3E,0XFF,0X00,0X05,0XFF//20KHZ);
	lcm_dcs_write_seq_static(ctx,0XF1,0X5A,0X59);
	lcm_dcs_write_seq_static(ctx,0XF0,0XA5,0XA6);
	lcm_dcs_write_seq_static(ctx,0X35,0X00);
	lcm_dcs_write_seq_static(ctx,0X11);
	udelay(120);
	lcm_dcs_write_seq_static(ctx,0X29);
	udelay(10);
	lcm_dcs_write_seq_static(ctx,0X26,0X01);//出睡眠增加的代码
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
	
        printk("icnl9911c %s:%d",__func__,__LINE__);
	lcm_dcs_write_seq_static(ctx,0X26,0X08);          // 进睡眠增加的代码
	lcm_dcs_write_seq_static(ctx,0X28);
	udelay(10);
	lcm_dcs_write_seq_static(ctx,0X10);
	udelay(120);

	ctx->error = 0;
	ctx->prepared = false;
#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
	lcm_panel_bias_disable();
#else
        printk("icnl9911c %s:%d",__func__,__LINE__);
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
        printk("icnl9911c %s:%d",__func__,__LINE__);
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

#define HFP (48)
#define HSA (4)
#define HBP (48)
#define VFP (150)
#define VSA (4)
#define VBP (12)
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
	strcpy(current_lcm_info.chip,"icnl9911c");
	strcpy(current_lcm_info.vendor,"unknow");
    sprintf(current_lcm_info.id,"0x%04x",0x9911);
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
	{ .compatible = "icnl9911c,boe", },
	{ }
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-icnl9911c-boe",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_DESCRIPTION("ICNL9911C BOE LCD Panel Driver");
MODULE_LICENSE("GPL v2");
