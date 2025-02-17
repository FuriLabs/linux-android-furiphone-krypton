/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2016 MediaTek Inc.
 */


#include <generated/autoconf.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
//#include <linux/pm_wakeup.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/writeback.h>
#include <linux/seq_file.h>
#include <linux/power_supply.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/reboot.h>

#include <mt-plat/v1/mtk_battery.h>
#include <mt-plat/upmu_common.h>
#include <mach/upmu_sw.h>
#include <mach/upmu_hw.h>
#include <mt-plat/mtk_boot.h>
#include <mt-plat/v1/charger_type.h>
#include <mt-plat/v1/charger_class.h>

#define __SW_CHRDET_IN_PROBE_PHASE__

static enum charger_type g_chr_type;
#ifdef __SW_CHRDET_IN_PROBE_PHASE__
static struct work_struct chr_work;
#endif

#if defined(CONFIG_CHARGER_SGM41516D)
static struct charger_device *primary_charger;
static int first_connect;
#endif

static DEFINE_MUTEX(chrdet_lock);

static struct power_supply *chrdet_psy;
static int chrdet_inform_psy_changed(enum charger_type chg_type,
				bool chg_online)
{
	int ret = 0;
	union power_supply_propval propval;

	pr_debug("charger type: %s: online = %d, type = %d\n",
		__func__, chg_online, chg_type);

	/* Inform chg det power supply */
	if (chg_online) {
		propval.intval = chg_online;
		ret = power_supply_set_property(chrdet_psy,
			POWER_SUPPLY_PROP_ONLINE, &propval);
		if (ret < 0)
			pr_debug("%s: psy online failed, ret = %d\n",
				__func__, ret);

		propval.intval = chg_type;
		ret = power_supply_set_property(chrdet_psy,
			POWER_SUPPLY_PROP_CHARGE_TYPE, &propval);
		if (ret < 0)
			pr_debug("%s: psy type failed, ret = %d\n",
				__func__, ret);

		return ret;
	}

	propval.intval = chg_type;
	ret = power_supply_set_property(chrdet_psy,
		POWER_SUPPLY_PROP_CHARGE_TYPE, &propval);
	if (ret < 0)
		pr_debug("%s: psy type failed, ret(%d)\n",
			__func__, ret);

	propval.intval = chg_online;
	ret = power_supply_set_property(chrdet_psy,
		POWER_SUPPLY_PROP_ONLINE, &propval);
	if (ret < 0)
		pr_debug("%s: psy online failed, ret(%d)\n",
			__func__, ret);
	return ret;
}


int hw_charging_get_charger_type(void)
{
#if !defined(CONFIG_CHARGER_SGM41516D)
	return STANDARD_HOST;
#else
	enum charger_type chr_type;
	int timeout = 200;
	int boot_mode = get_boot_mode();

	pr_debug("hw_bc11_init boot_mode = %d\n", boot_mode);

	msleep(200);
	if (boot_mode != RECOVERY_BOOT) {
		if (first_connect == true) {
			if (is_usb_rdy() == false) {
				while (is_usb_rdy() == false && timeout > 0) {
					msleep(100);
					timeout--;
				}
				if (timeout == 0)
					pr_debug("CDP, timeout\n");
				else
					pr_debug("CDP, free\n");
			} else
				pr_debug("CDP, pass\n");
			first_connect = false;
		}
	}
	chr_type = charger_dev_get_ext_chgtyp(primary_charger);
	return chr_type;
#endif
}

/*****************************************************************************
 * Charger Detection
 ******************************************************************************/
void __attribute__((weak)) mtk_pmic_enable_chr_type_det(bool en)
{
}

void do_charger_detect(void)
{
	if (!mt_usb_is_device()) {
		g_chr_type = CHARGER_UNKNOWN;
		pr_debug("charger type: UNKNOWN, Now is usb host mode. Skip detection!!!\n");
		return;
	}

	mutex_lock(&chrdet_lock);

	if (pmic_get_register_value(PMIC_RGS_CHRDET)) {
		pr_debug("charger type: charger IN\n");
		g_chr_type = hw_charging_get_charger_type();
		chrdet_inform_psy_changed(g_chr_type, 1);
	} else {
		pr_debug("charger type: charger OUT\n");
		g_chr_type = CHARGER_UNKNOWN;
		chrdet_inform_psy_changed(g_chr_type, 0);
	}

	mutex_unlock(&chrdet_lock);
}



/*****************************************************************************
 * PMIC Int Handler
 ******************************************************************************/
void chrdet_int_handler(void)
{
	/*
	 * pr_debug("[chrdet_int_handler]CHRDET status = %d....\n",
	 *	pmic_get_register_value(PMIC_RGS_CHRDET));
	 */
#ifdef CONFIG_MTK_KERNEL_POWER_OFF_CHARGING
	if (!pmic_get_register_value(PMIC_RGS_CHRDET)) {
		int boot_mode = 0;

		boot_mode = get_boot_mode();

		if (boot_mode == KERNEL_POWER_OFF_CHARGING_BOOT ||
		    boot_mode == LOW_POWER_OFF_CHARGING_BOOT) {
			pr_debug("[%s] Unplug Charger/USB\n", __func__);
#ifndef CONFIG_TCPC_CLASS
			orderly_poweroff(true);
#else
			return;
#endif
		}
	}
#endif
	do_charger_detect();
}


/************************************************
 * Charger Probe Related
 ************************************************/
#ifdef __SW_CHRDET_IN_PROBE_PHASE__
static void do_charger_detection_work(struct work_struct *data)
{
	if (pmic_get_register_value(PMIC_RGS_CHRDET))
		do_charger_detect();
}
#endif

static int __init pmic_chrdet_init(void)
{
	mutex_init(&chrdet_lock);
	chrdet_psy = power_supply_get_by_name("charger");
	if (!chrdet_psy) {
		pr_debug("%s: get power supply failed\n", __func__);
		return -EINVAL;
	}

#if defined(CONFIG_CHARGER_SGM41516D)
	primary_charger = get_charger_by_name("primary_chg");
	if (!primary_charger) {
		pr_debug("%s: get primary charger device failed\n", __func__);
		return -EINVAL;
	}
	first_connect = true;
#endif

#ifdef __SW_CHRDET_IN_PROBE_PHASE__
	/* do charger detect here to prevent HW miss interrupt*/
	INIT_WORK(&chr_work, do_charger_detection_work);
	schedule_work(&chr_work);
#endif

	pmic_register_interrupt_callback(INT_CHRDET_EDGE, chrdet_int_handler);
	pmic_enable_interrupt(INT_CHRDET_EDGE, 1, "PMIC");

	return 0;
}

late_initcall(pmic_chrdet_init);
