/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#define PFX "CAM_CAL"
#define pr_fmt(fmt) PFX "[%s] " fmt, __func__

#include "eeprom_utils.h"
#include <linux/ktime.h>

#define EEPROM_PROF 1

#if EEPROM_PROF
void EEPROM_PROFILE_INIT(struct timeval *ptv)
{
	do_gettimeofday(ptv);
}

void EEPROM_PROFILE(struct timeval *ptv, char *tag)
{
	struct timeval tv;
	unsigned long  time_interval;

	do_gettimeofday(&tv);
	time_interval =
	    (tv.tv_sec - ptv->tv_sec) * 1000000 + (tv.tv_usec - ptv->tv_usec);

	pr_debug("[%s]Profile = %lu us\n", tag, time_interval);
}

#else
void EEPROM_PROFILE_INIT(struct timeval *ptv) {}
void EEPROM_PROFILE(struct timeval *ptv, char *tag) {}
#endif

