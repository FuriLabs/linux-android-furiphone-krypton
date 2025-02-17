/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef __IMGSENSOR_HW_REGULATOR_H__
#define __IMGSENSOR_HW_REGULATOR_H__

#include <linux/of.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include "imgsensor_hw.h"
#include "imgsensor_common.h"

enum REGULATOR_VOLTAGE {
	REGULATOR_VOLTAGE_0    = 0,
	REGULATOR_VOLTAGE_1000 = 1000000,
	REGULATOR_VOLTAGE_1100 = 1100000,
	REGULATOR_VOLTAGE_1200 = 1200000,
	REGULATOR_VOLTAGE_1210 = 1210000,
	REGULATOR_VOLTAGE_1220 = 1220000,
	REGULATOR_VOLTAGE_1500 = 1500000,
	REGULATOR_VOLTAGE_1800 = 1800000,
	REGULATOR_VOLTAGE_2500 = 2500000,
	REGULATOR_VOLTAGE_2800 = 2800000,
	REGULATOR_VOLTAGE_2900 = 2900000,
};
/*prize add by zhuzhengjiang for camera ldo start*/
#ifdef CONFIG_PRIZE_CAMERA_LDO_WL2868C
enum wl2868c_ldo_num {
	WL2868C_LDO1 = 1,
	WL2868C_LDO2 = 2,
	WL2868C_LDO3 = 3,
	WL2868C_LDO4 = 4,
	WL2868C_LDO5 = 5,
	WL2868C_LDO6 = 6,
	WL2868C_LDO7 = 7,
};
#endif
/*prize add by zhuzhengjiang for camera ldo end*/
enum REGULATOR_TYPE {
	REGULATOR_TYPE_VCAMA,
#ifdef CONFIG_REGULATOR_RT5133
	REGULATOR_TYPE_VCAMA1,
#endif
#if defined(IMGSENSOR_MT6781) || defined(IMGSENSOR_MT6877)
	REGULATOR_TYPE_VCAMAF,
#endif
	REGULATOR_TYPE_VCAMD,
	REGULATOR_TYPE_VCAMIO,
	REGULATOR_TYPE_MAX_NUM
};

struct REGULATOR_CTRL {
	char *pregulator_type;
};

struct REGULATOR {
	struct regulator *pregulator[
		IMGSENSOR_SENSOR_IDX_MAX_NUM][REGULATOR_TYPE_MAX_NUM];
	atomic_t          enable_cnt[
		IMGSENSOR_SENSOR_IDX_MAX_NUM][REGULATOR_TYPE_MAX_NUM];
};

enum IMGSENSOR_RETURN imgsensor_hw_regulator_open(
	struct IMGSENSOR_HW_DEVICE **pdevice);

#endif

