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
#include <linux/fb.h>

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mtk_panel_ext.h"
#include "../mediatek/mtk_log.h"
#include "../mediatek/mtk_drm_graphics_base.h"
#endif

#ifdef CONFIG_MTK_ROUND_CORNER_SUPPORT
#include "../mediatek/mtk_corner_pattern/mtk_data_hw_roundedpattern.h"
#endif
#include "../../../misc/prize/lcd_bias/prize_lcd_bias.h"
//#include "prize_lcd_bias.h"
static struct lcm *local_lcm;

#if defined(CONFIG_PRIZE_HARDWARE_INFO)
#include "../../../misc/mediatek/hardware_info/hardware_info.h"
extern struct hardware_info current_lcm_info;
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

static struct notifier_block fb_nb;
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

static void lcm_panel_init(struct lcm *ctx)
{
	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return;
	}
	printk("liaojie lcm init !\n");
	//gpiod_set_value(ctx->reset_gpio, 0);
	//udelay(15 * 1000);
	//gpiod_set_value(ctx->reset_gpio, 1);
	//msleep(10);
	gpiod_set_value(ctx->reset_gpio, 0);
	msleep(20);
	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(10);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	//----------------------LCD initial code End----------------------//
//Displaypowersetting
lcm_dcs_write_seq_static(ctx,0xF0,0x50,0x78,0x94);
lcm_dcs_write_seq_static(ctx,0xb0,0x23);//VGHO=6.5
lcm_dcs_write_seq_static(ctx,0xb1,0x32);//VGLO=-8V
lcm_dcs_write_seq_static(ctx,0xba,0x05);//AVEEPUMPCLK
lcm_dcs_write_seq_static(ctx,0xbb,0x04);//VCLPUMPCLK
lcm_dcs_write_seq_static(ctx,0xbc,0x85);//VGHPUMP
lcm_dcs_write_seq_static(ctx,0xbd,0x85);//VGLPUMP
lcm_dcs_write_seq_static(ctx,0xb2,0x14);			//VREFP1=2.5V
lcm_dcs_write_seq_static(ctx,0xb3,0x30);			//VREFN=-5VVint=-1.917V
lcm_dcs_write_seq_static(ctx,0xC0,0x2A);//AVDD=7.3V--2A
lcm_dcs_write_seq_static(ctx,0xC1,0x16,0x16);//ELVSS=-3.0V,forA50:-6.4(0pulse)~-2.4(40pulse)****************************
lcm_dcs_write_seq_static(ctx,0xC2,0x35);//ELVDD=4.6V
lcm_dcs_write_seq_static(ctx,0xC3,0x45,0x04);				//avdd,elvsssendpulse
lcm_dcs_write_seq_static(ctx,0xC4,0x00);//ELVDD/ELVSS/AVDDfreshper1secondOFF
lcm_dcs_write_seq_static(ctx,0xCE,0xAA,0xAA,0xAA,0xAA,0x0A);
lcm_dcs_write_seq_static(ctx,0xd0,0x06,0x1D,0x40,0x0F,0x13,0x2C,0x36,0x1D);
lcm_dcs_write_seq_static(ctx,0xd2,0x06,0x00,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66);
lcm_dcs_write_seq_static(ctx,0xD5,0x13);
lcm_dcs_write_seq_static(ctx,0xd6,0xAA,0xAA,0xAA,0xAA,0x0A);
lcm_dcs_write_seq_static(ctx,0xd1,0x4a,0x4a,0x80,0x02,0x91);//sourceoutputV255inpoweronblankframe,forpreventingAVDDpowerdownNEW
lcm_dcs_write_seq_static(ctx,0xd3,0x11,0x00,0x10,0x64);
lcm_dcs_write_seq_static(ctx,0xd4,0x10,0x06,0x06,0x0B,0x0B,0x06,0x06,0x06);
lcm_dcs_write_seq_static(ctx,0xd7,0xA8,0xAA,0xA5,0xAA,0x0A);
lcm_dcs_write_seq_static(ctx,0xd8,0xA8,0xAA,0xAA,0xAA,0x0A);
lcm_dcs_write_seq_static(ctx,0xDD,0xA8,0xAA,0xAA,0xAA,0x0A);
lcm_dcs_write_seq_static(ctx,0xDE,0x63,0x50,0x55);
lcm_dcs_write_seq_static(ctx,0xDF,0x00,0x04);

//BC--0nits--400nits--500nits
//255-192��ratio
//192-40��ste
//40-0:ratio
lcm_dcs_write_seq_static(ctx,0xF0,0x54,0x78,0x94);
lcm_dcs_write_seq_static(ctx,0xC0,0x83,0xFF,0x00,0x05);  //10bit
lcm_dcs_write_seq_static(ctx,0xC1,0xC0,0xA0,0x80,0x60,0x40,0x30,0x28,0x00);
lcm_dcs_write_seq_static(ctx,0xC2,0xFF);
lcm_dcs_write_seq_static(ctx,0xC3,0xAB,0x86);
lcm_dcs_write_seq_static(ctx,0xC4,0x0C);
lcm_dcs_write_seq_static(ctx,0xC5,0x50,0x0C,0x95,0x0D,0x74,0x55,0xC8,0xEC,0xFE,0xFE);
lcm_dcs_write_seq_static(ctx,0xC6,0xFF,0xFF);
lcm_dcs_write_seq_static(ctx,0xC7,0x5A);
lcm_dcs_write_seq_static(ctx,0xC9,0xC0,0x40,0x30,0x28,0x20,0x18,0x10,0x08,0x04,0x02,0x01,0x00);
lcm_dcs_write_seq_static(ctx,0xCA,0xFF,0x8F);
lcm_dcs_write_seq_static(ctx,0xCB,0x76,0x34,0x33,0x33,0x12,0x00);
lcm_dcs_write_seq_static(ctx,0xCC,0xFF,0x84,0x84,0x84,0x84,0x6B,0x37,0xE9,0x84,0xF7,0x15,0x94,0x5F,0x3A,0x00);
lcm_dcs_write_seq_static(ctx,0xCD,0x02,0x04,0x04,0x20,0x02,0x02);
																																																
//AOD
lcm_dcs_write_seq_static(ctx,0xF0,0x59,0x78,0x94);
lcm_dcs_write_seq_static(ctx,0xE4,0x94);
lcm_dcs_write_seq_static(ctx,0xE5,0x30,0x11,0x52,0x10);//exitelvdd/elvss
lcm_dcs_write_seq_static(ctx,0xEA,0x03);

//Displaytimesetting
lcm_dcs_write_seq_static(ctx,0xF0,0x59,0x78,0x94);
lcm_dcs_write_seq_static(ctx,0xb0,0x0b,0x15,0x20);//VBP=16VFP=16VTotal=2432
lcm_dcs_write_seq_static(ctx,0xb2,0x40);//SW_M=0,RES=01080CRL=0
lcm_dcs_write_seq_static(ctx,0xb3,0x01,0x18);//NL=1280+1060(265)2340
lcm_dcs_write_seq_static(ctx,0xb4,0x61,0x24,0x61);//RGBforedopanel
lcm_dcs_write_seq_static(ctx,0xb5,0x00);//2Rx3CRML=0
lcm_dcs_write_seq_static(ctx,0xb6,0x00,0x34,0x5D);//SDT=0.25usSOURCE1=2.5us*26.5=60SOURCE2=3.5us*26.5=
lcm_dcs_write_seq_static(ctx,0xb8,0x02,0x62);//STEPEQS1EQS2EQSTSDCLK=2*OSC
lcm_dcs_write_seq_static(ctx,0xb7,0x02,0x02,0x02,0x02);//STVRSTVFCLKRCLKF
lcm_dcs_write_seq_static(ctx,0xC3,0x00);
lcm_dcs_write_seq_static(ctx,0xc9,0x22,0xF0,0xF0);//Internalhsset,HTime=6.85usCLK=6.85x106=726(2D6)
lcm_dcs_write_seq_static(ctx,0xb9,0x00);//00video11cmd
lcm_dcs_write_seq_static(ctx,0xbb,0x03,0x07);//--------------------20221207
lcm_dcs_write_seq_static(ctx,0xbc,0x37,0x07,0x52,0x84);//--------------------20221207
lcm_dcs_write_seq_static(ctx,0xbd,0x30);								//chop

//GOAtimingfor643--EDOSXD
lcm_dcs_write_seq_static(ctx,0xF0,0x58,0x78,0x94);//Page8---------------------------------used
lcm_dcs_write_seq_static(ctx,0xB0,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xC6,0x00);//--------------------20221207
lcm_dcs_write_seq_static(ctx,0xC7,0x02,0x03,0x13,0x12,0x08,0x09,0x09,0x0B,0x0B,0x0A,0x0A,0x14);//GOUTR
//SCK2_RSCK1_RECK2_RECK1_RSIN_RMUX1MUX2
lcm_dcs_write_seq_static(ctx,0xC8,0x02,0x03,0x13,0x12,0x08,0x09,0x09,0x0B,0x0B,0x0A,0x0A,0x14);//GOUTL
//SCK2_LSCK1_LECK2_LECK1_LEIN_LMUX1MUX2

lcm_dcs_write_seq_static(ctx,0xB3,0xA9,0x28,0x5F,0x0C,0x03,0x48,0xA2);//STV1(02)-----STE
lcm_dcs_write_seq_static(ctx,0xB4,0x22,0x00,0x00,0x01,0x00,0x35,0x08);//STV2(03)-----STV
lcm_dcs_write_seq_static(ctx,0xB9,0x21,0xA0,0x00,0x6A,0x05,0x32);//CKV1(08)-----CKE1
lcm_dcs_write_seq_static(ctx,0xBA,0x23,0xA0,0x00,0x6A,0x05,0x32);//CKV2(09)-----CKE2
lcm_dcs_write_seq_static(ctx,0xBB,0x21,0x80,0x00,0x54,0xA4,0x10);//CKV3(0A)-----CKV1//--------------------20230410
lcm_dcs_write_seq_static(ctx,0xBC,0x20,0x80,0x00,0x54,0xA4,0x10);//CKV4(0B)-----CKV2//--------------------20230410
lcm_dcs_write_seq_static(ctx,0xC3,0x20,0x01,0x00,0x00,0x33);//SW1(12)-----SW1//MUX1
lcm_dcs_write_seq_static(ctx,0xC4,0x20,0x40,0x00,0x00,0x33);//SW2(13)-----SW2//MUX2

//gamma----0.3---0.31--500nits
lcm_dcs_write_seq_static(ctx,0xF0,0x57,0x78,0x94);
lcm_dcs_write_seq_static(ctx,0xB0,0x21,0x58,0xF5);//VGSP=2.7V,VGMP=6V
  //500   X=0.28 Y=0.32
//  lcm_dcs_write_seq_static(ctx,0xB0,0x21,0x58,0xF5});  //VGSP=2.7V, VGMP = 6V
//Red     550    0.305    0.32
lcm_dcs_write_seq_static(ctx,0xb2,0x00,0x00,0x48,0x90,0xc0,0x54,0xf0,0x0f,0x28,0x3e,0x55,0x63,0x81,0x9d,0xb5);
lcm_dcs_write_seq_static(ctx,0xb3,0xa9,0xe2,0x0a,0x2f,0x53,0xaa,0x65,0x77,0x9a,0xbd,0xfa,0xe1,0xf2,0x03,0x19);
lcm_dcs_write_seq_static(ctx,0xb4,0xff,0x2d,0x42,0x58,0x6c,0xff,0x82,0x8d,0x92,0x97,0x0f,0xa0,0xa1);
//Green
lcm_dcs_write_seq_static(ctx,0xb5,0x00,0x00,0x4d,0x9a,0xce,0x55,0x01,0x18,0x2f,0x42,0x55,0x66,0x84,0x9f,0xb8);
lcm_dcs_write_seq_static(ctx,0xb6,0xa9,0xe5,0x0c,0x31,0x54,0xaa,0x66,0x78,0x9b,0xbd,0xfa,0xe0,0xf0,0x02,0x17);
lcm_dcs_write_seq_static(ctx,0xb7,0xff,0x2a,0x3f,0x53,0x65,0xff,0x7b,0x86,0x8a,0x8f,0x0f,0x97,0x98);
//Blue
lcm_dcs_write_seq_static(ctx,0xb8,0x00,0x00,0x55,0xaa,0xe2,0x55,0x1a,0x36,0x4f,0x65,0x55,0x8c,0xad,0xcb,0xe6);
lcm_dcs_write_seq_static(ctx,0xb9,0xaa,0x16,0x40,0x68,0x8d,0xaa,0xa0,0xb3,0xd7,0xf9,0xff,0x21,0x33,0x47,0x5d);
lcm_dcs_write_seq_static(ctx,0xba,0xff,0x71,0x86,0x9a,0xad,0xff,0xc3,0xd0,0xd7,0xdd,0x0f,0xe8,0xe9);

/*
//Red     0.31   0.32
lcm_dcs_write_seq_static(ctx,0xb2,0x00,0x00,0x2c,0x58,0x76,0x00,0x94,0xb0,0xca,0xe1,0x55,0x07,0x28,0x44,0x5d);
lcm_dcs_write_seq_static(ctx,0xb3,0x55,0x88,0xb1,0xd7,0xfa,0xaa,0x0b,0x1b,0x3c,0x5f,0xaa,0x83,0x93,0xa4,0xb6);
lcm_dcs_write_seq_static(ctx,0xb4,0xea,0xc8,0xda,0xee,0x00,0xff,0x16,0x22,0x28,0x2d,0x0f,0x34,0x37);
//Green
lcm_dcs_write_seq_static(ctx,0xb5,0x00,0x00,0x32,0x64,0x85,0x00,0xa6,0xbb,0xd3,0xe7,0x55,0x0d,0x2d,0x49,0x62);
lcm_dcs_write_seq_static(ctx,0xb6,0x55,0x8e,0xb7,0xdc,0xff,0xaa,0x10,0x20,0x41,0x64,0xaa,0x87,0x98,0xa8,0xb9);
lcm_dcs_write_seq_static(ctx,0xb7,0xea,0xcb,0xdd,0xf0,0x02,0xff,0x16,0x21,0x26,0x2b,0x0f,0x31,0x34);
//Blue
lcm_dcs_write_seq_static(ctx,0xb8,0x00,0x00,0x38,0x71,0x97,0x40,0xbd,0xd7,0xf3,0x0b,0x55,0x37,0x59,0x78,0x93);
lcm_dcs_write_seq_static(ctx,0xb9,0xa5,0xc6,0xf1,0x18,0x3d,0xaa,0x50,0x63,0x89,0xab,0xea,0xd0,0xe2,0xf5,0x08);
lcm_dcs_write_seq_static(ctx,0xba,0xff,0x1c,0x30,0x45,0x58,0xff,0x6d,0x78,0x7e,0x83,0x0f,0x89,0x8d);
*/
/*
//Red     0.29     0.29
lcm_dcs_write_seq_static(ctx,0xb2,0x00,0x00,0x4a,0x94,0xc6,0x54,0xf7,0x17,0x31,0x41,0x55,0x61,0x7c,0x95,0xac);
lcm_dcs_write_seq_static(ctx,0xb3,0xa5,0xd3,0xf5,0x14,0x32,0xaa,0x41,0x50,0x6e,0x8d,0xaa,0xab,0xba,0xca,0xda);
lcm_dcs_write_seq_static(ctx,0xb4,0xfa,0xea,0xfa,0x0a,0x1b,0xff,0x2c,0x35,0x39,0x3e,0x0f,0x42,0x44);
//Green
lcm_dcs_write_seq_static(ctx,0xb5,0x00,0x00,0x4f,0x9e,0xd2,0x55,0x06,0x1f,0x36,0x46,0x55,0x65,0x7f,0x99,0xaf);
lcm_dcs_write_seq_static(ctx,0xb6,0xa5,0xd7,0xf8,0x18,0x36,0xaa,0x44,0x54,0x73,0x90,0xaa,0xae,0xbd,0xcd,0xdc);
lcm_dcs_write_seq_static(ctx,0xb7,0xfa,0xec,0xfd,0x0c,0x1d,0xff,0x2d,0x36,0x3a,0x3e,0x0f,0x42,0x45);
//Blue
lcm_dcs_write_seq_static(ctx,0xb8,0x00,0x00,0x56,0xac,0xe5,0x55,0x1e,0x3f,0x5b,0x6b,0x55,0x90,0xb0,0xcb,0xe3);
lcm_dcs_write_seq_static(ctx,0xb9,0xaa,0x0d,0x32,0x57,0x79,0xaa,0x8a,0x9a,0xba,0xdb,0xfe,0xfe,0x0e,0x1f,0x30);
lcm_dcs_write_seq_static(ctx,0xba,0xff,0x41,0x53,0x65,0x77,0xff,0x8a,0x93,0x98,0x9d,0x0f,0xa2,0xa5);
*/
//Red
//lcm_dcs_write_seq_static(ctx,0xb2,0x00,0x00,0x34,0x68,0x8a,0x40,0xad,0xcd,0xed,0x0d,0x55,0x2d,0x4d,0x67,0x7d);
//lcm_dcs_write_seq_static(ctx,0xb3,0x95,0xa7,0xcb,0xeb,0x0a,0xaa,0x19,0x29,0x46,0x63,0xaa,0x80,0x8e,0x9d,0xab);
//lcm_dcs_write_seq_static(ctx,0xb4,0xaa,0xba,0xc9,0xd9,0xe8,0xfa,0xf7,0xff,0x02,0x07,0x0f,0x0a,0x0d);
//Green
//lcm_dcs_write_seq_static(ctx,0xb5,0x00,0x00,0x3a,0x74,0x9a,0x40,0xc1,0xd7,0xf7,0x17,0x55,0x33,0x53,0x6e,0x85);
//lcm_dcs_write_seq_static(ctx,0xb6,0x95,0xaf,0xd3,0xf4,0x13,0xaa,0x22,0x31,0x4f,0x6c,0xaa,0x89,0x98,0xa6,0xb5);
//lcm_dcs_write_seq_static(ctx,0xb7,0xaa,0xc4,0xd3,0xe2,0xf1,0xff,0x01,0x09,0x0d,0x10,0x0f,0x14,0x16);
//Blue
//lcm_dcs_write_seq_static(ctx,0xb8,0x00,0x00,0x42,0x84,0xb0,0x50,0xdc,0xfc,0x1c,0x3c,0x55,0x5c,0x7c,0x99,0xb2);
//lcm_dcs_write_seq_static(ctx,0xb9,0xa9,0xdf,0x06,0x29,0x4b,0xaa,0x5b,0x6b,0x8a,0xa9,0xaa,0xc7,0xd7,0xe6,0xf6);
//lcm_dcs_write_seq_static(ctx,0xba,0xff,0x06,0x16,0x26,0x36,0xff,0x46,0x4e,0x52,0x56,0x0f,0x5a,0x5c);
//Red
//lcm_dcs_write_seq_static(ctx,0xb2,0x00,0x00,0x2f,0x5e,0x7e,0x00,0x9e,0xc0,0xde,0xf3,0x55,0x15,0x32,0x4c,0x63);
//lcm_dcs_write_seq_static(ctx,0xb3,0x55,0x8a,0xb1,0xd6,0xf6,0xaa,0x04,0x14,0x32,0x52,0xaa,0x71,0x82,0x90,0xa0);
//lcm_dcs_write_seq_static(ctx,0xb4,0xaa,0xb0,0xc3,0xd4,0xe5,0xfa,0xf5,0xfe,0x02,0x07,0x0f,0x09,0x0a);
//Green
//lcm_dcs_write_seq_static(ctx,0xb5,0x00,0x00,0x36,0x6b,0x8f,0x00,0xb3,0xc9,0xe2,0xf5,0x55,0x14,0x31,0x4a,0x61);
//lcm_dcs_write_seq_static(ctx,0xb6,0x55,0x88,0xae,0xd2,0xf2,0xaa,0x00,0x10,0x2e,0x4f,0xaa,0x6c,0x7c,0x89,0x99);
//lcm_dcs_write_seq_static(ctx,0xb7,0xaa,0xa9,0xbb,0xcb,0xdc,0xaa,0xec,0xf5,0xfa,0xfe,0x0f,0x00,0x01);
//Blue
//lcm_dcs_write_seq_static(ctx,0xb8,0x00,0x00,0x3c,0x79,0xa1,0x40,0xc9,0xdf,0xfd,0x14,0x55,0x39,0x5a,0x76,0x90);
//lcm_dcs_write_seq_static(ctx,0xb9,0xa5,0xbb,0xe5,0x0c,0x2f,0xaa,0x3e,0x4f,0x6f,0x91,0xaa,0xb1,0xc2,0xd0,0xe1);
//lcm_dcs_write_seq_static(ctx,0xba,0xfe,0xf2,0x06,0x17,0x29,0xff,0x39,0x43,0x47,0x4c,0x0f,0x4f,0x50);//7.65002

//Sprsetting----------------2022.12.13
lcm_dcs_write_seq_static(ctx,0x5B,0x01);
lcm_dcs_write_seq_static(ctx,0xF0,0x52,0x78,0x94);
lcm_dcs_write_seq_static(ctx,0xB0,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xB1,0x10,0x00);
lcm_dcs_write_seq_static(ctx,0xB2,0x1A,0x08,0x47,0x64,0x1C,0x12,0x64);
lcm_dcs_write_seq_static(ctx,0xB3,0x7F,0x7F,0x00,0x32,0xAA,0x21,0x00,0x77,0x87);
lcm_dcs_write_seq_static(ctx,0xB4,0x2A,0xA9,0x2A,0x00,0x87,0x77,0x21,0xAA,0x32);
lcm_dcs_write_seq_static(ctx,0xB5,0x32,0xAA,0x21,0x77,0x87,0x00,0x32,0xAA,0x21);
lcm_dcs_write_seq_static(ctx,0xB6,0x00,0x87,0x77,0x21,0xAA,0x32,0x87,0x77,0x00);
lcm_dcs_write_seq_static(ctx,0xB7,0x34,0xAA,0x22,0x34,0xAA,0x22,0x5E,0xA2,0x00);
lcm_dcs_write_seq_static(ctx,0xB8,0x34,0xAA,0x22,0x34,0xAA,0x22,0x5E,0xA2,0x00);
lcm_dcs_write_seq_static(ctx,0xB9,0x34,0xAA,0x22,0x34,0xAA,0x22,0x5E,0xA2,0x00);
lcm_dcs_write_seq_static(ctx,0xBA,0x34,0xAA,0x22,0x34,0xAA,0x22,0x5E,0xA2,0x00);
lcm_dcs_write_seq_static(ctx,0xBB,0x34,0xAA,0x22,0x34,0xAA,0x22,0x5E,0xA2,0x00);
lcm_dcs_write_seq_static(ctx,0xBC,0x34,0xAA,0x22,0x34,0xAA,0x22,0x5E,0xA2,0x00);
lcm_dcs_write_seq_static(ctx,0xBD,0x34,0xAA,0x22,0x34,0xAA,0x22,0x5E,0xA2,0x00);
lcm_dcs_write_seq_static(ctx,0xBE,0x34,0xAA,0x22,0x34,0xAA,0x22,0x5E,0xA2,0x00);
lcm_dcs_write_seq_static(ctx,0xBF,0x7F,0x7F,0x00,0x32,0xAA,0x21,0x00,0x77,0x87);
lcm_dcs_write_seq_static(ctx,0xC0,0x2A,0xA9,0x2A,0x00,0x87,0x77,0x21,0xAA,0x32);
lcm_dcs_write_seq_static(ctx,0xC1,0x32,0xAA,0x21,0x77,0x87,0x00,0x32,0xAA,0x21);
lcm_dcs_write_seq_static(ctx,0xC2,0x00,0x87,0x77,0x21,0xAA,0x32,0x87,0x77,0x00);
lcm_dcs_write_seq_static(ctx,0xC3,0x7F,0x7F,0x00,0x32,0xAA,0x21,0x00,0x77,0x87);
lcm_dcs_write_seq_static(ctx,0xC4,0x2A,0xA9,0x2A,0x00,0x87,0x77,0x21,0xAA,0x32);
lcm_dcs_write_seq_static(ctx,0xC5,0x32,0xAA,0x21,0x77,0x87,0x00,0x32,0xAA,0x21);
lcm_dcs_write_seq_static(ctx,0xC6,0x00,0x87,0x77,0x21,0xAA,0x32,0x87,0x77,0x00);
lcm_dcs_write_seq_static(ctx,0xC7,0x7F,0x7F,0x00,0x32,0xAA,0x21,0x00,0x77,0x87);
lcm_dcs_write_seq_static(ctx,0xC8,0x32,0xAA,0x21,0x77,0x87,0x00,0x32,0xAA,0x21);
lcm_dcs_write_seq_static(ctx,0xC9,0x2A,0xA9,0x2A,0x00,0x87,0x77,0x21,0xAA,0x32);
lcm_dcs_write_seq_static(ctx,0xCA,0x00,0x87,0x77,0x21,0xAA,0x32,0x87,0x77,0x00);
lcm_dcs_write_seq_static(ctx,0xCB,0x00,0x1F,0xAF,0x3F,0xBF,0x00,0x1F,0xBF,0x1F);
lcm_dcs_write_seq_static(ctx,0xCC,0x00,0xAF,0x1F,0x00,0xBF,0x3F,0x00,0x7F,0x7F);
lcm_dcs_write_seq_static(ctx,0xCD,0xA2,0x4F,0x13,0x23);

//notch?and?rouder
//lcm_dcs_write_seq_static(ctx,0x57,0x3F);?
lcm_dcs_write_seq_static(ctx,0x57,0x3F);
lcm_dcs_write_seq_static(ctx,0xf0,0x55,0x78,0x94);
lcm_dcs_write_seq_static(ctx,0xB0,0x77,0x08,0xF1,0x03,0x54,0x54);
lcm_dcs_write_seq_static(ctx,0xB1,0x21,0xFD,0x3E,0x00,0x61);
lcm_dcs_write_seq_static(ctx,0xED,0x21,0x20,0x18,0x1B,0x00);
//For?top?rounder?corner
lcm_dcs_write_seq_static(ctx,0xB2,0x1F,0x42,0x76,0xA8,0xCB,0xDC,0xEE,0xFF,0x18,0x42,0x97,0xDB,0x6F,0x21,0x85,0xEB);
lcm_dcs_write_seq_static(ctx,0xB3,0x15,0x52,0xB8,0x14,0x84,0x5C,0x21,0x96,0x4F,0x21,0xA6,0x13,0xA5,0x14,0x84,0x3E);
lcm_dcs_write_seq_static(ctx,0xB4,0x51,0x4C,0x21,0xE7,0x13,0x83,0x13,0x93,0x13,0xA3,0x13,0x93,0x22,0x37,0x61,0x3E);
lcm_dcs_write_seq_static(ctx,0xB5,0x41,0x2D,0x92,0x13,0xE5,0x22,0x3B,0x51,0x2E,0xA2,0x13,0xE4,0x12,0x27,0xB2,0x32);
lcm_dcs_write_seq_static(ctx,0xB6,0x3D,0x51,0x2F,0x81,0x12,0x2A,0xC2,0x22,0x2D,0xE3,0x32,0x2E,0xE3,0x32,0x2F,0xF3);
lcm_dcs_write_seq_static(ctx,0xB7,0x32,0x2E,0xE2,0x22,0x2E,0xD1,0x12,0x2C,0xA1,0x12,0x17,0x27,0xE2,0x12,0x2D,0xA1);
lcm_dcs_write_seq_static(ctx,0xB8,0x91,0x22,0x2F,0xD1,0x12,0x19,0x26,0xD1,0x12,0x19,0x26,0xD1,0x91,0x12,0x2F,0xA1);
lcm_dcs_write_seq_static(ctx,0xB9,0x61,0x12,0x1C,0x27,0xD1,0x81,0x12,0x1D,0x28,0xD1,0x71,0x12,0x1D,0x26,0xB1,0x41);
lcm_dcs_write_seq_static(ctx,0xBA,0xA1,0x12,0x1E,0x27,0xC1,0x31,0x81,0x12,0x1D,0x14,0x29,0xD1,0x31,0x71,0xC1,0x12);
lcm_dcs_write_seq_static(ctx,0xBB,0x1E,0x15,0x19,0x2C,0xF1,0x41,0x81,0xB1,0x12,0x1D,0x12,0x14,0x17,0x19,0x1C,0x2E);
lcm_dcs_write_seq_static(ctx,0xBC,0xF1,0x31,0x51,0x61,0x81,0x91,0xA1,0xB1,0xC1,0xD1,0xE1,0xE1,0xF1,0xF1,0xF1,0x00);
//For?bottom?rounder?corner
lcm_dcs_write_seq_static(ctx,0xC2,0xF1,0xF1,0xF1,0xE1,0xE1,0xD1,0xC1,0xB1,0xA1,0x91,0x81,0x61,0x51,0x31,0x11,0x12);
lcm_dcs_write_seq_static(ctx,0xC3,0x1E,0x1C,0x1A,0x17,0x15,0x22,0xE1,0xB1,0x81,0x51,0x12,0x1F,0x1D,0x19,0x25,0xF1);
lcm_dcs_write_seq_static(ctx,0xC4,0xC1,0x81,0x31,0x12,0x1D,0x19,0x24,0xD1,0x91,0x31,0x12,0x1C,0x27,0xE1,0xA1,0x41);
lcm_dcs_write_seq_static(ctx,0xC5,0x12,0x1C,0x26,0xD1,0x81,0x12,0x1E,0x28,0xE1,0x81,0x12,0x1D,0x28,0xC1,0x61,0x12);
lcm_dcs_write_seq_static(ctx,0xC6,0x2B,0xF1,0x91,0x12,0x1D,0x26,0x91,0x12,0x1D,0x26,0x91,0x12,0x2C,0xF2,0x81,0x12);
lcm_dcs_write_seq_static(ctx,0xC7,0x29,0xC1,0x12,0x2E,0xF3,0x81,0x12,0x27,0x91,0x12,0x2A,0xB1,0x12,0x2C,0xC1,0x12);
lcm_dcs_write_seq_static(ctx,0xC8,0x2C,0xC1,0x12,0x2B,0xA1,0x12,0x28,0x61,0x13,0xF4,0x32,0x2D,0xB2,0x12,0x37,0x41);
lcm_dcs_write_seq_static(ctx,0xC9,0x2E,0xB2,0x13,0xF6,0x32,0x3C,0x61,0x2F,0xB3,0x13,0xE5,0x13,0xF7,0x32,0x39,0x41);
lcm_dcs_write_seq_static(ctx,0xCA,0x3B,0x41,0x3B,0x41,0x4A,0x31,0xF8,0x23,0xE7,0x13,0xA5,0x14,0x62,0x4C,0x31,0xC7);
lcm_dcs_write_seq_static(ctx,0xCB,0x15,0x63,0xFA,0x24,0x85,0x6C,0x21,0x96,0xEC,0x17,0x63,0xB8,0xFD,0x1F,0x42,0x76);
lcm_dcs_write_seq_static(ctx,0xCC,0xA9,0xCB,0xED,0xFE,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
//Notch
lcm_dcs_write_seq_static(ctx,0xD0,0xF8,0xAD,0x58,0x23,0x41,0xBF,0x27,0xF4,0x8D,0x32,0xDF,0x26,0x7E,0xF3,0x2A,0xF2);
lcm_dcs_write_seq_static(ctx,0xD1,0x27,0x3D,0xC2,0x22,0x1C,0xC2,0x21,0x2D,0xF2,0x23,0x7F,0xA1,0xE2,0x22,0x7F,0xD1);
lcm_dcs_write_seq_static(ctx,0xD2,0xF2,0x16,0x1D,0x28,0x2F,0xB1,0x71,0xF2,0x12,0x1D,0x1A,0x18,0x15,0x13,0x12,0x11);
lcm_dcs_write_seq_static(ctx,0xD3,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xD4,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xD5,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xD6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xD7,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xD8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xD9,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xDD,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xDE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xDF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE1,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE2,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE3,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE4,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE5,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE7,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
lcm_dcs_write_seq_static(ctx,0xE8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);

lcm_dcs_write_seq_static(ctx,0x6E,0x01);
lcm_dcs_write_seq_static(ctx,0xF0,0x54,0x78,0x94);
lcm_dcs_write_seq_static(ctx,0xB0,0x13);//lineblocklimit
lcm_dcs_write_seq_static(ctx,0xB1,0x02,0x58,0x01,0x2C);//limitvalue
lcm_dcs_write_seq_static(ctx,0xB3,0x03,0x00);//location
lcm_dcs_write_seq_static(ctx,0xB2,0x00,0xAA,0x00,0xAA,0x00,0xAA,0x00,0xAA,0x00,0xAA,0x00,0xAA,0x00,0xAA,0x00,0xAA,0x00,0xAA);//coff
lcm_dcs_write_seq_static(ctx,0xB5,0x02,0x06);//CT_N_L,CT_N_B
lcm_dcs_write_seq_static(ctx,0xB8,0x03,0x00,0x03,0x80,0x04,0x00,0x04,0x80,0x05,0x00,0x06,0x00,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);//V_block
lcm_dcs_write_seq_static(ctx,0xB9,0x20,0x1F,0x1E,0x1D,0x1C,0x1A,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);//VB
lcm_dcs_write_seq_static(ctx,0xBB,0x66,0x66,0x77,0x00,0x00,0x00,0x00,0x00);//V_block_inter
lcm_dcs_write_seq_static(ctx,0xB6,0x04,0x80,0x05,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);//V_line
lcm_dcs_write_seq_static(ctx,0xB7,0x20,0x22,0x26,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);//VL
lcm_dcs_write_seq_static(ctx,0xBA,0x76,0x00,0x00,0x00,0x00,0x00,0x00,0x00);//V_line_inter
		lcm_dcs_write_seq_static(ctx,0x11,0);
	msleep(120);//��ʱλ�õ���
	lcm_dcs_write_seq_static(ctx,0x29,0);
	msleep(50);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
		
	if (!ctx->enabled)
		return 0;
	pr_err("%s\n", __func__);
	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}
	pr_err("%s_end\n", __func__);
	ctx->enabled = false;

	return 0;
}
static int lcd_power_off = 0;
static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	pr_err("%s\n", __func__);
	lcd_power_off = 1;
	if (!ctx->prepared)
		return 0;

	lcm_dcs_write_seq_static(ctx, 0x28);
	msleep(50);
	lcm_dcs_write_seq_static(ctx, 0x10);
	msleep(150);

	ctx->error = 0;
	ctx->prepared = false;
	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
#if defined(CONFIG_PRIZE_LCD_BIAS)
	//display_bias_disable();
#else
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
	msleep(20);

	return 0;
}

static void prize_lcm_power_off(void)
{
	
	if(lcd_power_off == 1)
	{
		printk("liaojie lcm power off down \n");
		return;
	}
		
	lcm_dcs_write_seq_static(local_lcm, 0x51,0x00,0x00);
	msleep(50);
	lcm_dcs_write_seq_static(local_lcm, 0x28);
	msleep(50);
	lcm_dcs_write_seq_static(local_lcm, 0x10);
	msleep(150);
	printk("liaojie lcm power off \n");
}
EXPORT_SYMBOL(prize_lcm_power_off);
static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;
	lcd_power_off = 0;
	pr_err("%s\n", __func__);
	if (ctx->prepared)
		return 0;

#if defined(CONFIG_PRIZE_LCD_BIAS)
	display_ldo18_enable(1);
	msleep(10);
	display_bias_enable();
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

	msleep(10);
	msleep(10);
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
	pr_err("%s\n", __func__);
	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}
	pr_err("%s_end\n", __func__);
	ctx->enabled = true;

	return 0;
}

#define HFP (20)
#define HSA (15)
#define HBP (20)
#define VFP (16)
#define VSA (4)
#define VBP (12)
#define VAC (2400)
#define HAC (1080)
static const struct drm_display_mode default_mode = {
	.clock = 153204,     //htotal*vtotal*vrefresh/1000   163943   182495
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
	unsigned char id[3] = {0x06, 0x11, 0x0C};
	ssize_t ret;

	ret = mipi_dsi_dcs_read(dsi, 0x4, data, 3);
	if (ret < 0) {
		pr_err("%s error\n", __func__);
		return 0;
	}

	printk("ATA read data %x %x %x\n", data[0], data[1], data[2]);

	if (data[0] == id[0] &&
			data[1] == id[1] &&
			data[2] == id[2])
		return 1;

	printk("ATA expect read data is %x %x %x\n",
			id[0], id[1], id[2]);

	return 0;
}

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb,
	void *handle, unsigned int level)
{
	unsigned char bl_tb0[] = {0x51, 0x00, 0x00};
	printk("liaojie led level = %d, bl high 8bit = 0x%08x,bl low 8bit = 0x%08x \n",level,bl_tb0[1],bl_tb0[2]);
	//level = level*179/256;
	printk("liaojie decresh led level = %d, bl high 8bit = 0x%08x,bl low 8bit = 0x%08x \n",level,bl_tb0[1],bl_tb0[2]);
	level = (level*1024)/256;
	bl_tb0[1] = (level>>8)&0xf;
	bl_tb0[2] = (level)&0xff;

	printk("liaojie final led level = %d, bl high 8bit = 0x%08x,bl low 8bit = 0x%08x \n",level,bl_tb0[1],bl_tb0[2]);
	if(level <= 0)
		level = 1;
	if (!cb)
		return -1;

	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));

	return 0;
}

static struct mtk_panel_params ext_params = {
	.pll_clk = 536,
	.vfp_low_power = 16,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A,
		.count = 1,
		.para_list[0] = 0x98, 
		.mask_list[0] = 0x98, 
	},
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 90,
	},
};

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ata_check = panel_ata_check,
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

	panel->connector->display_info.width_mm = 67;
	panel->connector->display_info.height_mm = 148;//sqrt((size*25.4)^2/(1600^2+720^2))*1600   26,674/

	return 1;
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};
static int fb_notifier_callback(struct notifier_block *nb, unsigned long event, void *data){
	struct fb_event *fb_event = data;
	int *blank;

	if (event != FB_EVENT_BLANK){
		return 0;
	}

	if (fb_event && fb_event->data) {
		blank = fb_event->data;
		printk("ot8201b Notifier's event = %ld, blank:%d truly\n", event, *blank);
		switch (*blank) {
			case FB_BLANK_POWERDOWN:
				display_bias_disable();
				msleep(5);
				display_ldo18_enable(0);   //prize---hjw
				break;
		}
	}

	return 0;
}

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
				pr_err("No panel connected,skip probe lcm\n");
				return -ENODEV;
			}
			pr_err("device node name:%s\n", remote_node->name);
		}
	}
	if (remote_node != dev->of_node) {
		pr_err("%s+ skip probe due to not current lcm\n", __func__);
		return -ENODEV;
	}

	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE
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

	fb_nb.notifier_call = fb_notifier_callback;
	if (fb_register_client(&fb_nb)){
		dev_err(dev, "%s: cannot register fb notify\n", __func__);
	}
	pr_err("%s-\n", __func__);
	local_lcm = ctx;
	
#if defined(CONFIG_PRIZE_HARDWARE_INFO)
    strcpy(current_lcm_info.chip,"ot8201b");
    strcpy(current_lcm_info.vendor,"aox");
    //sprintf(current_lcm_info.id,"0x%02x",0x02);
    strcpy(current_lcm_info.more,"1080x2400");
#endif
	
	return ret;
}

static int lcm_remove(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);

	fb_unregister_client(&fb_nb);
	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id lcm_of_match[] = {
	{ .compatible = "aox,ot8201b,a201", },
	{ }
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-aox-ot8201b-a201",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("Yi-Lun Wang <Yi-Lun.Wang@mediatek.com>");
MODULE_DESCRIPTION("truly td4330 CMD LCD Panel Driver");
MODULE_LICENSE("GPL v2");
