// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include "regulator.h"
/*prize add by zhuzhengjiang for camera ldo start*/
#ifdef CONFIG_PRIZE_CAMERA_LDO_WL2868C
extern int wl2868c_voltage_output(unsigned int ldo_num,unsigned int vol);
#endif
/*prize add by zhuzhengjiang for camera ldo end*/
static const int regulator_voltage[] = {
	REGULATOR_VOLTAGE_0,
	REGULATOR_VOLTAGE_1000,
	REGULATOR_VOLTAGE_1100,
	REGULATOR_VOLTAGE_1200,
	REGULATOR_VOLTAGE_1210,
	REGULATOR_VOLTAGE_1220,
	REGULATOR_VOLTAGE_1500,
	REGULATOR_VOLTAGE_1800,
	REGULATOR_VOLTAGE_2500,
	REGULATOR_VOLTAGE_2800,
	REGULATOR_VOLTAGE_2900,
};

struct REGULATOR_CTRL regulator_control[REGULATOR_TYPE_MAX_NUM] = {
	{"vcama"},
#ifdef CONFIG_REGULATOR_RT5133
	{"vcama1"},
#endif
#if defined(IMGSENSOR_MT6781) || defined(IMGSENSOR_MT6877)
	{"vcamaf"},
#endif
	{"vcamd"},
	{"vcamio"},
};

static struct REGULATOR reg_instance;
/*prize add by zhuzhengjiang for sham dual camera  20201208 start*/
#ifdef CONFIG_PRIZE_DUAL_CAMERA_ENABLE
static struct regulator *g_avdd_pregulator;
static struct regulator *g_dvdd_pregulator;
void set_avdd_regulator(int status)
{
	PK_INFO("set_avdd_regulator status=%d\n",status);
	if(g_avdd_pregulator!=NULL){
		if(status ==1)
		{
		if (regulator_set_voltage(g_avdd_pregulator,REGULATOR_VOLTAGE_2800,REGULATOR_VOLTAGE_2800)){
			PK_INFO("[regulator]fail to regulator_set_voltage avdd\n");
		}
		if (regulator_enable(g_avdd_pregulator)) {
			PK_INFO("[regulator]fail to regulator_enable, powertype\n");
		}
	}else{
		if (regulator_disable(g_avdd_pregulator)) {
			PK_INFO("[regulator]fail to avdd regulator_disable, powertype:\n");
		}
	}
	}else{
		PK_INFO("prize linchong g_avdd_pregulator is NULL\n");
	}
	
}
void set_dvdd_regulator(int status)
{
	PK_INFO("set_dvdd_regulator  status=%d\n",status);
	if(g_dvdd_pregulator!=NULL){
		if(status ==1)
		{
		if (regulator_set_voltage(g_dvdd_pregulator,REGULATOR_VOLTAGE_1800,REGULATOR_VOLTAGE_1800)){
			PK_INFO("[regulator]fail to regulator_set_voltage avdd\n");
		}
		if (regulator_enable(g_dvdd_pregulator)) {
			PK_INFO("[regulator]fail to regulator_enable, powertype\n");
		}
	}else{
		if (regulator_disable(g_dvdd_pregulator)) {
			PK_INFO("[regulator]fail to avdd regulator_disable, powertype:\n");
		}
	}
	}else{
		PK_INFO("prize linchong g_dvdd_pregulator is NULL\n");
	}
	
}
#endif
/*prize add by zhuzhengjiang for sham dual camera  20201208 end*/

static enum IMGSENSOR_RETURN regulator_init(
	void *pinstance,
	struct IMGSENSOR_HW_DEVICE_COMMON *pcommon)
{
	struct REGULATOR *preg = (struct REGULATOR *)pinstance;
	int type, idx, ret = 0;
	char str_regulator_name[LENGTH_FOR_SNPRINTF];

	for (idx = IMGSENSOR_SENSOR_IDX_MIN_NUM;
		idx < IMGSENSOR_SENSOR_IDX_MAX_NUM;
		idx++) {
		for (type = 0;
			type < REGULATOR_TYPE_MAX_NUM;
			type++) {
			memset(str_regulator_name, 0,
				sizeof(str_regulator_name));
			ret = snprintf(str_regulator_name,
				sizeof(str_regulator_name),
				"cam%d_%s",
				idx,
				regulator_control[type].pregulator_type);
			if (ret < 0)
				return ret;
			preg->pregulator[idx][type] = regulator_get_optional(
					&pcommon->pplatform_device->dev,
					str_regulator_name);
			if (IS_ERR(preg->pregulator[idx][type])) {
				preg->pregulator[idx][type] = NULL;
				PK_INFO("ERROR: regulator[%d][%d]  %s fail!\n",
						idx, type, str_regulator_name);
			}
			atomic_set(&preg->enable_cnt[idx][type], 0);
		}
	}
	return IMGSENSOR_RETURN_SUCCESS;
}

static enum IMGSENSOR_RETURN regulator_release(void *pinstance)
{
	struct REGULATOR *preg = (struct REGULATOR *)pinstance;
	int type, idx;
	struct regulator *pregulator = NULL;
	atomic_t *enable_cnt = NULL;

	for (idx = IMGSENSOR_SENSOR_IDX_MIN_NUM;
		idx < IMGSENSOR_SENSOR_IDX_MAX_NUM;
		idx++) {

		for (type = 0; type < REGULATOR_TYPE_MAX_NUM; type++) {
			pregulator = preg->pregulator[idx][type];
			enable_cnt = &preg->enable_cnt[idx][type];
			if (pregulator != NULL) {
				for (; atomic_read(enable_cnt) > 0; ) {
					regulator_disable(pregulator);
					atomic_dec(enable_cnt);
				}
			}
		}
	}
	return IMGSENSOR_RETURN_SUCCESS;
}

static enum IMGSENSOR_RETURN regulator_set(
	void *pinstance,
	enum IMGSENSOR_SENSOR_IDX   sensor_idx,
	enum IMGSENSOR_HW_PIN       pin,
	enum IMGSENSOR_HW_PIN_STATE pin_state)
{
	struct regulator     *pregulator;
	struct REGULATOR     *preg = (struct REGULATOR *)pinstance;
	int reg_type_offset;
	atomic_t             *enable_cnt;


	if (pin > IMGSENSOR_HW_PIN_DOVDD   ||
	    pin < IMGSENSOR_HW_PIN_AVDD    ||
	    pin_state < IMGSENSOR_HW_PIN_STATE_LEVEL_0 ||
	    pin_state >= IMGSENSOR_HW_PIN_STATE_LEVEL_HIGH)
		return IMGSENSOR_RETURN_ERROR;

	reg_type_offset = REGULATOR_TYPE_VCAMA;
/*prize add by zhuzhengjiang for camera ldo start*/
#ifdef CONFIG_PRIZE_CAMERA_LDO_WL2868C
	if(pin ==IMGSENSOR_HW_PIN_DOVDD) {
		wl2868c_voltage_output(WL2868C_LDO6,regulator_voltage[pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0]/1000);
		mdelay(10);
		return IMGSENSOR_RETURN_SUCCESS;
	}
	else if(pin ==IMGSENSOR_HW_PIN_AVDD) {
		wl2868c_voltage_output(WL2868C_LDO3 + sensor_idx,regulator_voltage[pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0]/1000);// main:ldo3 sub:ldo4 main2:ldo5
		if(sensor_idx == 0) {

			PK_DBG("prize add regulator_set af");
			wl2868c_voltage_output(WL2868C_LDO7,regulator_voltage[pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0]/1000);// af:ldo7
		}
		return IMGSENSOR_RETURN_SUCCESS;
	}
	else  if(pin ==IMGSENSOR_HW_PIN_DVDD) {
		wl2868c_voltage_output(WL2868C_LDO1 + sensor_idx,regulator_voltage[pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0]/1000);// main:ldo1 sub:ldo2
		return IMGSENSOR_RETURN_SUCCESS;
	}
#endif
/*prize add by zhuzhengjiang for camera ldo end*/
	pregulator = preg->pregulator[(unsigned int)sensor_idx][
		reg_type_offset + pin - IMGSENSOR_HW_PIN_AVDD];

	enable_cnt = &preg->enable_cnt[(unsigned int)sensor_idx][
		reg_type_offset + pin - IMGSENSOR_HW_PIN_AVDD];

	if (pregulator) {
		if (pin_state != IMGSENSOR_HW_PIN_STATE_LEVEL_0) {
			if (regulator_set_voltage(pregulator,
				regulator_voltage[
				pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0],
				regulator_voltage[
				pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0])) {

				PK_PR_ERR(
				  "[regulator]fail to regulator_set_voltage, powertype:%d powerId:%d\n",
				  pin,
				  regulator_voltage[
				  pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0]);
			}
			if (regulator_enable(pregulator)) {
				PK_PR_ERR(
				"[regulator]fail to regulator_enable, powertype:%d powerId:%d\n",
				pin,
				regulator_voltage[
				  pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0]);
				return IMGSENSOR_RETURN_ERROR;
			}
			atomic_inc(enable_cnt);
		} else {
			if (regulator_is_enabled(pregulator))
				PK_DBG("[regulator]%d is enabled\n", pin);

			if (regulator_disable(pregulator)) {
				PK_PR_ERR(
					"[regulator]fail to regulator_disable, powertype: %d\n",
					pin);
				return IMGSENSOR_RETURN_ERROR;
			}
			atomic_dec(enable_cnt);
		}
	} else {
		PK_PR_ERR("regulator == NULL %d %d %d\n",
				reg_type_offset,
				pin,
				IMGSENSOR_HW_PIN_AVDD);
	}

	return IMGSENSOR_RETURN_SUCCESS;
}

static enum IMGSENSOR_RETURN regulator_dump(void *pinstance)
{
	struct REGULATOR *preg = (struct REGULATOR *)pinstance;
	int i, j;
	int enable = 0;

	for (j = IMGSENSOR_SENSOR_IDX_MIN_NUM;
		j < IMGSENSOR_SENSOR_IDX_MAX_NUM;
		j++) {

		for (i = REGULATOR_TYPE_VCAMA;
		i < REGULATOR_TYPE_MAX_NUM;
		i++) {
			if (!preg->pregulator[j][i])
				continue;

			if (regulator_is_enabled(preg->pregulator[j][i]) &&
				atomic_read(&preg->enable_cnt[j][i]) != 0)
				enable = 1;
			else
				enable = 0;

			PK_DBG("[sensor_dump][regulator] index= %d, %s = %d, enable = %d\n",
				j,
				regulator_control[i].pregulator_type,
				regulator_get_voltage(preg->pregulator[j][i]),
				enable);
		}
	}
	return IMGSENSOR_RETURN_SUCCESS;
}

static struct IMGSENSOR_HW_DEVICE device = {
	.id        = IMGSENSOR_HW_ID_REGULATOR,
	.pinstance = (void *)&reg_instance,
	.init      = regulator_init,
	.set       = regulator_set,
	.release   = regulator_release,
	.dump      = regulator_dump
};

enum IMGSENSOR_RETURN imgsensor_hw_regulator_open(
	struct IMGSENSOR_HW_DEVICE **pdevice)
{
	*pdevice = &device;
	return IMGSENSOR_RETURN_SUCCESS;
}

