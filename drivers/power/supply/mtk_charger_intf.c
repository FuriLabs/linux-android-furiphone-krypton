// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

/*
 *
 * Filename:
 * ---------
 *    mtk_charger.c
 *
 * Project:
 * --------
 *   Android_Software
 *
 * Description:
 * ------------
 *   This Module defines functions of Battery charging
 *
 * Author:
 * -------
 * Wy Chuang
 *
 */
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/reboot.h>
#include <linux/mfd/mt6397/core.h>/* PMIC MFD core header */
#include <linux/regmap.h>
#include <linux/of_platform.h>

#include "mtk_charger.h"

/*prize-zhaopengge-20211230, add fuel gauge cw2017  start*/
#if defined(CONFIG_MTK_CW2017_SUPPORT)
extern int g_cw2017_capacity;
extern int g_cw2017_vol;
extern int cw2017_exit_flag;
extern int g_cw2017_bat_temperature_val;
#endif
/* prize-zhaopengge-20211230, add fuel gauge cw2017  end */

/*prize-sunshuai-20201224, add fuel gauge cw2015  start*/
#if defined(CONFIG_MTK_CW2015_SUPPORT)
extern int g_cw2015_capacity;
extern int g_cw2015_vol;
extern int cw2015_exit_flag;
#endif
/* prize-sunshuai-20201224, add fuel gauge cw2015  end */
/* prize modify by liaoxingen start */
#if defined(CONFIG_MTK_CW2217_SUPPORT)
extern int cw2217_exit_flag;
#endif
/* prize modify by liaoxingen end */
int get_uisoc(struct mtk_charger *info)
{
	union power_supply_propval prop;
	struct power_supply *bat_psy = NULL;
	int ret;

	bat_psy = info->bat_psy;

#if defined(CONFIG_MTK_CW2217_SUPPORT)
	if (cw2217_exit_flag == 1)
	bat_psy = power_supply_get_by_name("cw-bat");
#endif
	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s retry to get bat_psy\n", __func__);
		bat_psy = devm_power_supply_get_by_phandle(&info->pdev->dev, "gauge");
		info->bat_psy = bat_psy;
	}

	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s Couldn't get bat_psy\n", __func__);
		ret = 50;
	} else {
		ret = power_supply_get_property(bat_psy,
			POWER_SUPPLY_PROP_CAPACITY, &prop);
		ret = prop.intval;
	}
	
	/*prize-zhaopengge-20211230, add fuel gauge cw2017  start*/
#if defined(CONFIG_MTK_CW2017_SUPPORT)
	if(cw2017_exit_flag==1)
	{
		ret= g_cw2017_capacity;
//prize add by lipengpeng 20210819 start 
	}else{
		ret = power_supply_get_property(bat_psy,POWER_SUPPLY_PROP_CAPACITY, &prop);
		ret = prop.intval;
	    printk("lpp---cw2015 Not loaded ret=%d\n",ret);
	}
//prize add by lipengpeng 20210819 end 
	chr_err("%s cw2015  g_cw2015_capacity  =%d\n", __func__,ret);
#endif
/*prize-zhaopengge-20211230, add fuel gauge cw2017  end*/

/* prize-sunshuai-20201224, add fuel gauge cw2015  start*/
#if defined(CONFIG_MTK_CW2015_SUPPORT)
	if(cw2015_exit_flag==1)
	{
		ret= g_cw2015_capacity;
//prize add by lipengpeng 20210819 start 
	}else{
		ret = power_supply_get_property(bat_psy,POWER_SUPPLY_PROP_CAPACITY, &prop);
		ret = prop.intval;
	    printk("lpp---cw2015 Not loaded ret=%d\n",ret);
	}
//prize add by lipengpeng 20210819 end 
	chr_err("%s cw2015  g_cw2015_capacity  =%d\n", __func__,ret);
#endif
/* prize-sunshuai-20201224, add fuel gauge cw2015  end*/

	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

int get_battery_voltage(struct mtk_charger *info)
{
	union power_supply_propval prop;
	struct power_supply *bat_psy = NULL;
	int ret;

	bat_psy = info->bat_psy;
#if defined(CONFIG_MTK_CW2217_SUPPORT)
	if (cw2217_exit_flag == 1)
	bat_psy = power_supply_get_by_name("cw-bat");
#endif
	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s retry to get bat_psy\n", __func__);
		bat_psy = devm_power_supply_get_by_phandle(&info->pdev->dev, "gauge");
		info->bat_psy = bat_psy;
	}

	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s Couldn't get bat_psy\n", __func__);
		ret = 3999;
	} else {
		ret = power_supply_get_property(bat_psy,
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &prop);
		ret = prop.intval / 1000;
	}
	
	/*prize-zhaopengge-20211230, add fuel gauge cw2017  start*/
#if defined(CONFIG_MTK_CW2017_SUPPORT)
		if(cw2017_exit_flag==1)
		{
			ret = g_cw2017_vol;
//prize add by lipengpeng 20210819 start 
		}else{
				ret = power_supply_get_property(bat_psy,POWER_SUPPLY_PROP_VOLTAGE_NOW, &prop);
				ret = prop.intval / 1000;
		        printk("lpp----cw2015 not loader ret=%d\n",ret);
		}
//prize add by lipengpeng 20210819 end 
		chr_err("%s cw2015	g_cw2015_vol  =%d\n", __func__,ret);
#endif
/*prize-zhaopengge-20211230, add fuel gauge cw2017  end*/

/* prize-sunshuai-20201224, add fuel gauge cw2015  start*/
#if defined(CONFIG_MTK_CW2015_SUPPORT)
		if(cw2015_exit_flag==1)
		{
			ret= g_cw2015_vol;
//prize add by lipengpeng 20210819 start 
		}else{
				ret = power_supply_get_property(bat_psy,POWER_SUPPLY_PROP_VOLTAGE_NOW, &prop);
				ret = prop.intval / 1000;
		        printk("lpp----cw2015 not loader ret=%d\n",ret);
		}
//prize add by lipengpeng 20210819 end 
		chr_err("%s cw2015	g_cw2015_vol  =%d\n", __func__,ret);
#endif
/* prize-sunshuai-20201224, add fuel gauge cw2015  end*/

	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

int get_battery_temperature(struct mtk_charger *info)
{
	union power_supply_propval prop;
	struct power_supply *bat_psy = NULL;
	int ret;

	bat_psy = info->bat_psy;
#if defined(CONFIG_MTK_CW2217_SUPPORT)
	if (cw2217_exit_flag == 1)
	bat_psy = power_supply_get_by_name("cw-bat");
#endif
	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s retry to get bat_psy\n", __func__);
		bat_psy = devm_power_supply_get_by_phandle(&info->pdev->dev, "gauge");
		info->bat_psy = bat_psy;
	}

	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s Couldn't get bat_psy\n", __func__);
		ret = 27;
	} else {
		ret = power_supply_get_property(bat_psy,
			POWER_SUPPLY_PROP_TEMP, &prop);
		ret = prop.intval / 10;
	}
	/*prize-zhaopengge-20211230, add fuel gauge cw2017  start*/
#if defined(CONFIG_MTK_CW2017_SUPPORT)
		if(cw2017_exit_flag==1)
		{
			ret = g_cw2017_bat_temperature_val / 10;
//prize add by lipengpeng 20210819 start 
		}else{
				ret = power_supply_get_property(bat_psy,POWER_SUPPLY_PROP_TEMP, &prop);
				ret = prop.intval / 10;
		        printk("lpp----cw2015 not loader ret=%d\n",ret);
		}
//prize add by lipengpeng 20210819 end 
		chr_err("%s cw2015	g_cw2015_vol  =%d\n", __func__,ret);
#endif
/*prize-zhaopengge-20211230, add fuel gauge cw2017  end*/

	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

int get_battery_current(struct mtk_charger *info)
{
	union power_supply_propval prop;
	struct power_supply *bat_psy = NULL;
	int ret;

	bat_psy = info->bat_psy;

	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s retry to get bat_psy\n", __func__);
		bat_psy = devm_power_supply_get_by_phandle(&info->pdev->dev, "gauge");
		info->bat_psy = bat_psy;
	}

	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s Couldn't get bat_psy\n", __func__);
		ret = 0;
	} else {
		ret = power_supply_get_property(bat_psy,
			POWER_SUPPLY_PROP_CURRENT_NOW, &prop);
		ret = prop.intval / 1000;
	}

	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

static int get_pmic_vbus(struct mtk_charger *info, int *vchr)
{
	union power_supply_propval prop;
	static struct power_supply *chg_psy;
	int ret;

	if (chg_psy == NULL)
		chg_psy = power_supply_get_by_name("mtk_charger_type");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		pr_notice("%s Couldn't get chg_psy\n", __func__);
		ret = -1;
	} else {
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &prop);
	}
	*vchr = prop.intval;

	pr_notice("%s vbus:%d\n", __func__,
		prop.intval);
	return ret;
}

//prize add by huarui, cc controller sgm7220, start
#if defined(CONFIG_TCPC_SGM7220)||defined(CONFIG_TCPC_WUSB3801)
int battery_get_vbus(void)
{
	int ret  = 0;
	int vbus = 0;

	ret = get_pmic_vbus(NULL, &vbus);

	return (ret < 0) ? ret : vbus;
}
#endif
//prize add by huarui, cc controller sgm7220, end

int get_vbus(struct mtk_charger *info)
{
	int ret = 0;
	int vchr = 0;

	if (info == NULL)
		return 0;
	ret = charger_dev_get_vbus(info->chg1_dev, &vchr);
	if (ret < 0) {
		ret = get_pmic_vbus(info, &vchr);
		if (ret < 0)
			chr_err("%s: get vbus failed: %d\n", __func__, ret);
	} else
		vchr /= 1000;

	return vchr;
}

int get_ibus(struct mtk_charger *info)
{
	int ret = 0;
	int ibus = 0;

	if (info == NULL)
		return -EINVAL;
	ret = charger_dev_get_ibus(info->chg1_dev, &ibus);
	if (ret < 0)
		pr_notice("%s: get ibus failed: %d\n", __func__, ret);

	return ibus / 1000;
}

bool is_battery_exist(struct mtk_charger *info)
{
	union power_supply_propval prop;
	struct power_supply *bat_psy = NULL;
	int ret;

	bat_psy = info->bat_psy;

	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s retry to get bat_psy\n", __func__);
		bat_psy = devm_power_supply_get_by_phandle(&info->pdev->dev, "gauge");
		info->bat_psy = bat_psy;
	}

	if (bat_psy == NULL || IS_ERR(bat_psy)) {
		chr_err("%s Couldn't get bat_psy\n", __func__);
		ret = 1;
	} else {
		ret = power_supply_get_property(bat_psy,
			POWER_SUPPLY_PROP_PRESENT, &prop);
		ret = prop.intval;
	}

	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

bool is_charger_exist(struct mtk_charger *info)
{
	union power_supply_propval prop;
	static struct power_supply *chg_psy;
	int ret;

	chg_psy = info->chg_psy;

	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s retry to get chg_psy\n", __func__);
		chg_psy = devm_power_supply_get_by_phandle(&info->pdev->dev, "charger");
		info->chg_psy = chg_psy;
	}

	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		pr_notice("%s Couldn't get chg_psy\n", __func__);
		ret = -1;
	} else {
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_ONLINE, &prop);
		ret = prop.intval;
	}

	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

int get_charger_type(struct mtk_charger *info)
{
	union power_supply_propval prop, prop2, prop3;
	static struct power_supply *chg_psy;
	int ret;

	prop.intval = 0;
	prop2.intval = 0;
	prop3.intval = 0;
	chg_psy = info->chg_psy;

	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s retry to get chg_psy\n", __func__);
		chg_psy = devm_power_supply_get_by_phandle(&info->pdev->dev, "charger");
		info->chg_psy = chg_psy;
	}

	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		pr_notice("%s Couldn't get chg_psy\n", __func__);
	} else {
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_ONLINE, &prop);

		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_TYPE, &prop2);

		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_USB_TYPE, &prop3);

		if (prop.intval == 0)
			prop2.intval = POWER_SUPPLY_TYPE_UNKNOWN;
		else if (prop2.intval == POWER_SUPPLY_TYPE_USB &&
		    prop3.intval == POWER_SUPPLY_USB_TYPE_UNKNOWN)
			prop2.intval = POWER_SUPPLY_TYPE_UNKNOWN;
		else if (prop2.intval == POWER_SUPPLY_TYPE_USB &&
		    prop3.intval == POWER_SUPPLY_USB_TYPE_DCP)
			prop2.intval = POWER_SUPPLY_TYPE_USB_FLOAT;
	}

	pr_notice("%s online:%d type:%d usb_type:%d\n", __func__,
		prop.intval,
		prop2.intval,
		prop3.intval);

	return prop2.intval;
}

int get_charger_temperature(struct mtk_charger *info,
	struct charger_device *chg)
{
	int ret = 0;
	int tchg_min = 0, tchg_max = 0;

	if (info == NULL)
		return 0;

	ret = charger_dev_get_temperature(chg, &tchg_min, &tchg_max);
	if (ret < 0)
		chr_err("%s: get temperature failed: %d\n", __func__, ret);
	else
		ret = (tchg_max + tchg_min) / 2;

	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

int get_charger_charging_current(struct mtk_charger *info,
	struct charger_device *chg)
{
	int ret = 0;
	int olduA = 0;

	if (info == NULL)
		return 0;
	ret = charger_dev_get_charging_current(chg, &olduA);
	if (ret < 0)
		chr_err("%s: get charging current failed: %d\n", __func__, ret);
	else
		ret = olduA;

	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

int get_charger_input_current(struct mtk_charger *info,
	struct charger_device *chg)
{
	int ret = 0;
	int olduA = 0;

	if (info == NULL)
		return 0;
	ret = charger_dev_get_input_current(chg, &olduA);
	if (ret < 0)
		chr_err("%s: get input current failed: %d\n", __func__, ret);
	else
		ret = olduA;

	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

int get_charger_zcv(struct mtk_charger *info,
	struct charger_device *chg)
{
	int ret = 0;
	int zcv = 0;

	if (info == NULL)
		return 0;

	ret = charger_dev_get_zcv(chg, &zcv);
	if (ret < 0)
		chr_err("%s: get charger zcv failed: %d\n", __func__, ret);
	else
		ret = zcv;
	chr_debug("%s:%d\n", __func__,
		ret);
	return ret;
}

#define PMIC_RG_VCDT_HV_EN_ADDR		0xb88
#define PMIC_RG_VCDT_HV_EN_MASK		0x1
#define PMIC_RG_VCDT_HV_EN_SHIFT	11

static void pmic_set_register_value1(struct regmap *map,
	unsigned int addr,
	unsigned int mask,
	unsigned int shift,
	unsigned int val)
{
	regmap_update_bits(map,
		addr,
		mask << shift,
		val << shift);
}

unsigned int pmic_get_register_value1(struct regmap *map,
	unsigned int addr,
	unsigned int mask,
	unsigned int shift)
{
	unsigned int value = 0;

	regmap_read(map, addr, &value);
	value =
		(value &
		(mask << shift))
		>> shift;
	return value;
}

int disable_hw_ovp(struct mtk_charger *info, int en)
{
	struct device_node *pmic_node;
	struct platform_device *pmic_pdev;
	struct mt6397_chip *chip;
	struct regmap *regmap;

	pmic_node = of_parse_phandle(info->pdev->dev.of_node, "pmic", 0);
	if (!pmic_node) {
		chr_err("get pmic_node fail\n");
		return -1;
	}

	pmic_pdev = of_find_device_by_node(pmic_node);
	if (!pmic_pdev) {
		chr_err("get pmic_pdev fail\n");
		return -1;
	}
	chip = dev_get_drvdata(&(pmic_pdev->dev));

	if (!chip) {
		chr_err("get chip fail\n");
		return -1;
	}

	regmap = chip->regmap;

	pmic_set_register_value1(regmap,
		PMIC_RG_VCDT_HV_EN_ADDR,
		PMIC_RG_VCDT_HV_EN_SHIFT,
		PMIC_RG_VCDT_HV_EN_MASK,
		en);

	return 0;
}
