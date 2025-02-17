// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

/**
 * @file	mtk_eem.
 * @brief   Driver for EEM
 *
 */

#define __MTK_EEM_C__
/*=============================================================
 * Include files
 *=============================================================
 */

/* system includes */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/interrupt.h>
#include <linux/syscore_ops.h>
#include <linux/platform_device.h>
#include <linux/completion.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/math64.h>
#include <linux/uaccess.h>
#include <linux/unistd.h>
#include <linux/rtc.h>

#ifdef CONFIG_OF
	#include <linux/cpu.h>
	#include <linux/of.h>
	#include <linux/of_irq.h>
	#include <linux/of_address.h>
	#include <linux/of_fdt.h>
	#include <mt-plat/aee.h>
#endif

#include <mt-plat/mtk_chip.h>
/* #include <mt-plat/mtk_gpio.h> */
#include "upmu_common.h"
#ifdef CONFIG_THERMAL
#include "mtk_thermal.h"
#endif
#include "mtk_ppm_api.h"
#include "mtk_cpufreq_api.h"
#include "mtk_eem_config.h"
#include "mtk_eem.h"
#include "mtk_defeem.h"
#include "mtk_eem_internal_ap.h"


#include "mtk_eem_internal.h"
#ifdef CONFIG_MTK_GPU_SUPPORT
#include "mtk_gpufreq.h"
#endif
#include <mt-plat/mtk_devinfo.h>
#include <regulator/consumer.h>
#include "pmic_regulator.h"
#include "pmic_api_buck.h"

#if UPDATE_TO_UPOWER
#include "mtk_upower.h"
#endif

#include "mtk_mcdi_api.h"

#ifdef CONFIG_MTK_TINYSYS_MCUPM_SUPPORT
#include <linux/soc/mediatek/mtk-mbox.h>
#include <linux/soc/mediatek/mtk_tinysys_ipi.h>
#include "mcupm_ipi_id.h"
#include "mcupm_driver.h"
#endif


/****************************************
 * define variables for legacy and eem
 ****************************************
 */

static unsigned int ctrl_EEMSN_Enable = 1;
static unsigned int ctrl_SN_Enable = 1;
#if defined(AGING_LOAD)
static unsigned char ctrl_agingload_enable;
#endif

/* Get time stmp to known the time period */
//static unsigned long long eem_pTime_us, eem_cTime_us, eem_diff_us;

/* for setting pmic pwm mode and auto mode */
//struct regulator *eem_regulator_vproc1;
//struct regulator *eem_regulator_vproc2;

static unsigned int eem_to_cputoeb(unsigned int cmd,
	struct eem_ipi_data *eem_data);

static int create_procfs(void);
#if UPDATE_TO_UPOWER
static void eem_update_init2_volt_to_upower
	(struct eemsn_det *det, unsigned int *pmic_volt);
static enum upower_bank transfer_ptp_to_upower_bank(unsigned int det_id);
#endif


static struct eemsn_devinfo eem_devinfo;
static unsigned int record_tbl_locked[NR_FREQ];


#define WAIT_TIME	(2500000)
#define FALL_NUM        (3)
#if SUPPORT_PICACHU
#define PICACHU_SIG					(0xA5)
#define PICACHU_SIGNATURE_SHIFT_BIT	(24)
#define EEM_PHY_TEMPSPARE1		0x11278F24

#endif
/******************************************
 * common variables for legacy ptp
 *******************************************
 */
static int eem_log_en;
static unsigned int eem_checkEfuse = 1;
static unsigned int informEEMisReady;
int ipi_ackdata;


phys_addr_t eem_log_phy_addr, eem_log_virt_addr;
uint32_t eem_log_size;
/* static unsigned int eem_disable = 1; */
#if 0
struct eemsn_log eemsn_log2;
struct eemsn_log *eemsn_log = &eemsn_log2;
#endif
struct eemsn_log *eemsn_log;
unsigned int seq;
/* Global variable for slow idle*/
unsigned int ptp_data[3] = {0, 0, 0};
static char *cpu_name[3] = {
	"L",
	"BIG",
	"CCI"
};


#ifdef CONFIG_OF
void __iomem *eem_base;
#endif

/*=============================================================
 * common functions for both ap and eem
 *=============================================================
 */
#if 0
#define EEM_IPI_SEND_DATA_LEN 4 /* size of cmd and args = 4 slot */
static unsigned int sspm_ipi_send_sync1(unsigned int cmd,
unsigned int cmd1, struct eem_ipi_data *eem_data, unsigned int len,
unsigned int *ackData, unsigned int xx)
{
	*ackData = 0;
	return 0;
}
#endif

static unsigned int eem_to_cputoeb(unsigned int cmd,
	struct eem_ipi_data *eem_data)
{
	//unsigned int len = EEM_IPI_SEND_DATA_LEN;
	unsigned int ret;

#if EEM_IPI_ENABLE
	eem_debug("to_cputoeb, cmd:%d\n", cmd);
#if 1
	//FUNC_ENTER(EEM_FUNC_LV_MODULE);
	eem_data->cmd = cmd;

#ifdef CONFIG_MTK_TINYSYS_MCUPM_SUPPORT
	ret = mtk_ipi_send_compl(&mcupm_ipidev, CH_S_EEMSN,
		/*IPI_SEND_WAIT*/IPI_SEND_POLLING, eem_data,
		sizeof(struct eem_ipi_data)/MBOX_SLOT_SIZE, 2000);
#else
	ret = 0;
#endif
	if (ret != 0)
		eem_error("IPI error(cmd:%d) ret:%d\n", cmd, ret);
	else if (ipi_ackdata < 0)
		eem_error("cmd(%d) return error ack(%d)\n",
			cmd, ipi_ackdata);
#else
	switch (cmd) {
	case IPI_EEMSN_SHARERAM_INIT:
		eem_data->cmd = cmd;
		ret = mtk_ipi_send_compl(&mcupm_ipidev, CH_S_EEMSN,
			IPI_SEND_POLLING, eem_data,
			sizeof(struct eem_ipi_data)/MBOX_SLOT_SIZE, 200);
		if (ret != 0)
			eem_debug("error(SHARERAM_INIT) ret:%d\n", ret);
		break;
	case IPI_EEMSN_INIT:
		eem_data->cmd = cmd;
		ret = mtk_ipi_send_compl(&mcupm_ipidev, CH_S_EEMSN,
			IPI_SEND_POLLING, eem_data,
			sizeof(struct eem_ipi_data)/MBOX_SLOT_SIZE, 2000);
		if (ret != 0)
			eem_debug("error(INIT) ret:%d\n", ret);
		break;
	case IPI_EEMSN_PROBE:
		eem_data->cmd = cmd;
		ret = mtk_ipi_send_compl(&mcupm_ipidev, CH_S_EEMSN,
			IPI_SEND_POLLING, eem_data,
			sizeof(struct eem_ipi_data)/MBOX_SLOT_SIZE, 2000);
		if (ret != 0)
			eem_debug("error(PROBE) ret:%d\n", ret);
		break;


	case IPI_EEMSN_GET_EEM_VOLT:
		eem_data->cmd = cmd;
		ret = mtk_ipi_send_compl(&mcupm_ipidev, CH_S_EEMSN,
			IPI_SEND_POLLING, eem_data,
			sizeof(struct eem_ipi_data)/MBOX_SLOT_SIZE, 2000);
		if (ret != 0)
			eem_debug("error(GET_EEM_VOLT) ret:%d\n", ret);
		break;


	case IPI_EEMSN_INIT02:
		eem_data->cmd = cmd;
		ret = mtk_ipi_send_compl(&mcupm_ipidev, CH_S_EEMSN,
			IPI_SEND_POLLING, eem_data,
			sizeof(struct eem_ipi_data)/MBOX_SLOT_SIZE, 200);
		if (ret != 0)
			eem_debug("error(IINIT02) ret:%d\n", ret);
		break;


	case IPI_EEMSN_DEBUG_PROC_WRITE:
		eem_data->cmd = cmd;
		ret = mtk_ipi_send_compl(&mcupm_ipidev, CH_S_EEMSN,
			IPI_SEND_POLLING, eem_data,
			sizeof(struct eem_ipi_data)/MBOX_SLOT_SIZE, 2000);
		if (ret != 0)
			eem_debug("error(DEBUG_PROC_WRITE) ret:%d\n", ret);
		break;

	case IPI_EEMSN_LOGEN_PROC_SHOW:
		eem_data->cmd = cmd;
		ret = mtk_ipi_send_compl(&mcupm_ipidev, CH_S_EEMSN,
			IPI_SEND_POLLING, eem_data,
			sizeof(struct eem_ipi_data)/MBOX_SLOT_SIZE, 2000);
		if (ret != 0)
			eem_debug("error(LOGEN_PROC_SHOW) ret:%d\n", ret);
		break;

	case IPI_EEMSN_LOGEN_PROC_WRITE:
		eem_data->cmd = cmd;
		ret = mtk_ipi_send_compl(&mcupm_ipidev, CH_S_EEMSN,
			IPI_SEND_POLLING, eem_data,
			sizeof(struct eem_ipi_data)/MBOX_SLOT_SIZE, 2000);
		if (ret != 0)
			eem_debug("error(LOGEN_PROC_WRITE) ret:%d\n", ret);
		break;

	case IPI_EEMSN_OFFSET_PROC_WRITE:
		eem_data->cmd = cmd;
		ret = mtk_ipi_send_compl(&mcupm_ipidev, CH_S_EEMSN,
			IPI_SEND_POLLING, eem_data,
			sizeof(struct eem_ipi_data)/MBOX_SLOT_SIZE, 2000);
		if (ret != 0)
			eem_debug("error(OFFSET_PROC_WRITE) ret:%d\n", ret);
		break;

	case IPI_EEMSN_CUR_VOLT_PROC_SHOW:
		eem_data->cmd = cmd;
		ret = mtk_ipi_send_compl(&mcupm_ipidev, CH_S_EEMSN,
			IPI_SEND_POLLING, eem_data,
			sizeof(struct eem_ipi_data)/MBOX_SLOT_SIZE, 2000);
		if (ret != 0)
			eem_debug("error(CUR_VOLT_PROC_SHOW) ret:%d\n",
						ret);
		break;

	case IPI_EEMSN_AGING_DUMP_PROC_SHOW:
		eem_data->cmd = cmd;
		ret = mtk_ipi_send_compl(&mcupm_ipidev, CH_S_EEMSN,
			IPI_SEND_POLLING, eem_data,
			sizeof(struct eem_ipi_data)/MBOX_SLOT_SIZE, 2000);
		if (ret != 0)
			eem_debug("error(AGING_DUMP_PROC_SHOW) ret:%d\n",
						ret);
		break;
	case IPI_EEMSN_EN_PROC_WRITE:
	case IPI_EEMSN_SNEN_PROC_WRITE:
	case IPI_EEMSN_FORCE_SN_SENSING:
	case IPI_EEMSN_PULL_DATA:
		eem_data->cmd = cmd;
		ret = mtk_ipi_send_compl(&mcupm_ipidev, CH_S_EEMSN,
			IPI_SEND_POLLING, eem_data,
			sizeof(struct eem_ipi_data)/MBOX_SLOT_SIZE, 2000);
		if (ret != 0)
			eem_debug("error(cmd:%d) ret:%d\n",
						cmd, ret);
		break;

	case IPI_EEMSN_EN_PROC_SHOW:
		eem_data->cmd = cmd;
		ret = mtk_ipi_send_compl(&mcupm_ipidev, CH_S_EEMSN,
			IPI_SEND_POLLING, eem_data,
			sizeof(struct eem_ipi_data)/MBOX_SLOT_SIZE, 2000);
		if (ret != 0)
			eem_debug("error(LOGEN_PROC_SHOW) ret:%d\n", ret);
		break;
	case IPI_EEMSN_SNEN_PROC_SHOW:
		eem_data->cmd = cmd;
		ret = mtk_ipi_send_compl(&mcupm_ipidev, CH_S_EEMSN,
			IPI_SEND_POLLING, eem_data,
			sizeof(struct eem_ipi_data)/MBOX_SLOT_SIZE, 2000);
		if (ret != 0)
			eem_debug("error(LOGEN_PROC_SHOW) ret:%d\n", ret);
		break;

	default:
			eem_debug("cmd(%d) wrong!!\n", cmd);
			break;
	}
#endif
	//FUNC_EXIT(EEM_FUNC_LV_MODULE);
#endif
	return ret;
}

unsigned int mt_eem_is_enabled(void)
{
	return informEEMisReady;
}

static struct eemsn_det *id_to_eem_det(enum eemsn_det_id id)
{
	if (likely(id < NR_EEMSN_DET))
		return &eemsn_detectors[id];
	else
		return NULL;
}

#if SUPPORT_PICACHU
#ifndef MC50_LOAD
static void get_picachu_efuse(void)
{
	int *val;
	phys_addr_t picachu_mem_base_phys;
	phys_addr_t picachu_mem_size;
	phys_addr_t picachu_mem_base_virt = 0;
	unsigned int i, cnt, sig;
	void __iomem *addr_ptr;

	val = (int *)&eem_devinfo;

	picachu_mem_size = 0x80000;
	picachu_mem_base_phys = eem_read(EEM_TEMPSPARE0);
	if ((void __iomem *)picachu_mem_base_phys != NULL)
		picachu_mem_base_virt =
			(phys_addr_t)(uintptr_t)ioremap_wc(
			picachu_mem_base_phys,
			picachu_mem_size);

#if 0
	eem_error("phys:0x%llx, size:0x%llx, virt:0x%llx\n",
		(unsigned long long)picachu_mem_base_phys,
		(unsigned long long)picachu_mem_size,
		(unsigned long long)picachu_mem_base_virt);
#endif
	if ((void __iomem *)(picachu_mem_base_virt) != NULL) {
		/* 0x60000 was reserved for eem efuse using */
		addr_ptr = (void __iomem *)(picachu_mem_base_virt
			+ 0x60000);

		/* check signature */
		sig = (eem_read(addr_ptr) >>
			PICACHU_SIGNATURE_SHIFT_BIT) & 0xff;

		if (sig == PICACHU_SIG) {
			cnt = eem_read(addr_ptr) & 0xff;
			if (cnt > IDX_HW_RES_SN)
				cnt = IDX_HW_RES_SN;
			addr_ptr += 4;

			/* check efuse data */
			for (i = 1; i < cnt; i++) {
				if (((i == 3) || (i == 4) || (i == 7)) &&
				(eem_read(addr_ptr + i * 4) == 0)) {
					eem_error("Wrong PI-OD%d: 0x%x\n",
						i, eem_read(addr_ptr + i * 4));
					return;
				}
			}

			for (i = 1; i < cnt; i++)
				val[i] = eem_read(addr_ptr + i * 4);

		}
	}
}
#endif
#endif

static int get_devinfo(void)
{

#if 0
	struct eemsn_det *det;
	unsigned int tmp;
	int err;
	struct rtc_device *rtc_dev;
	int efuse_val, year, mon, real_year, real_mon;
	struct rtc_time tm;
#endif
	int ret = 0, i = 0;
	int *val;
	unsigned int safeEfuse = 0, sn_safeEfuse = 0;

	FUNC_ENTER(FUNC_LV_HELP);

#if 0
	memset(&tm, 0, sizeof(struct rtc_time));
	rtc_dev = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc_dev) {
		err = rtc_read_time(rtc_dev, &tm);
		if (err < 0)
			pr_debug("fail to read time\n");
	} else
		pr_debug("[systimer] rtc_class_open rtc_dev fail\n");
#endif

	val = (int *)&eem_devinfo;

	/* FTPGM */
	val[0] = get_devinfo_with_index(DEVINFO_IDX_0);
	val[1] = get_devinfo_with_index(DEVINFO_IDX_1);
	val[2] = get_devinfo_with_index(DEVINFO_IDX_2);
	val[3] = get_devinfo_with_index(DEVINFO_IDX_3);
	val[4] = get_devinfo_with_index(DEVINFO_IDX_4);
	val[5] = get_devinfo_with_index(DEVINFO_IDX_5);
	val[6] = get_devinfo_with_index(DEVINFO_IDX_6);
	val[7] = get_devinfo_with_index(DEVINFO_IDX_7);
	val[8] = get_devinfo_with_index(DEVINFO_IDX_8);
	val[9] = get_devinfo_with_index(DEVINFO_IDX_9);
	val[10] = get_devinfo_with_index(DEVINFO_IDX_10);
	val[11] = get_devinfo_with_index(DEVINFO_IDX_11);
	val[12] = get_devinfo_with_index(DEVINFO_IDX_12);
	val[13] = get_devinfo_with_index(DEVINFO_IDX_13);
	val[14] = get_devinfo_with_index(DEVINFO_IDX_14);
	val[15] = get_devinfo_with_index(DEVINFO_IDX_15);
	val[16] = get_devinfo_with_index(DEVINFO_IDX_16);
	val[17] = get_devinfo_with_index(DEVINFO_IDX_17);
	val[18] = get_devinfo_with_index(DEVINFO_IDX_21);
	val[19] = get_devinfo_with_index(DEVINFO_IDX_22);
	val[20] = get_devinfo_with_index(DEVINFO_IDX_23);
	val[21] = get_devinfo_with_index(DEVINFO_IDX_24);
	val[22] = get_devinfo_with_index(DEVINFO_IDX_25);

#if 0
	efuse_val = get_devinfo_with_index(DEVINFO_TIME_IDX);
	year = ((efuse_val >> 4) & 0xf) + 2018;
	mon = efuse_val  & 0xf;
	real_year = tm.tm_year + 1900;
	real_mon = tm.tm_mon + 1;

	if (((real_year - year == 1) && (real_mon > mon)) ||
			(real_year - year > 1))
		time_val = 0;
#endif

#if EEM_FAKE_EFUSE
	/* for verification */
	val[0] = DEVINFO_0;
	val[1] = DEVINFO_1;
	val[2] = DEVINFO_2;
	val[3] = DEVINFO_3;
	val[4] = DEVINFO_4;
	val[5] = DEVINFO_5;
	val[6] = DEVINFO_6;
	val[7] = DEVINFO_7;
	val[8] = DEVINFO_8;
	val[9] = DEVINFO_9;
	val[10] = DEVINFO_10;
	val[11] = DEVINFO_11;
	val[12] = DEVINFO_12;
	val[13] = DEVINFO_13;
	val[14] = DEVINFO_14;
	val[15] = DEVINFO_15;
	val[16] = DEVINFO_16;
	val[17] = DEVINFO_17;
	val[18] = DEVINFO_21;
	val[19] = DEVINFO_22;
	val[20] = DEVINFO_23;
	val[21] = DEVINFO_24;
	val[22] = DEVINFO_25;

#endif

	for (i = 0; i < NR_HW_RES_FOR_BANK; i++)
		eem_debug("[PTP_DUMP] RES%d: 0x%X\n",
			i, val[i]);

#ifdef CONFIG_EEM_AEE_RR_REC
	aee_rr_rec_ptp_e0((unsigned int)val[0]);
	aee_rr_rec_ptp_e1((unsigned int)val[1]);
	aee_rr_rec_ptp_e2((unsigned int)val[2]);
	aee_rr_rec_ptp_e3((unsigned int)val[3]);
	aee_rr_rec_ptp_e4((unsigned int)val[4]);
	aee_rr_rec_ptp_e5((unsigned int)val[5]);
	aee_rr_rec_ptp_e6((unsigned int)val[6]);
	aee_rr_rec_ptp_e7((unsigned int)val[7]);
	aee_rr_rec_ptp_e8((unsigned int)val[8]);
	aee_rr_rec_ptp_e9((unsigned int)val[9]);
	aee_rr_rec_ptp_e10((unsigned int)val[10]);
	aee_rr_rec_ptp_e11((unsigned int)val[20]);
	aee_rr_rec_ptp_devinfo_0((unsigned int)val[21]);
	aee_rr_rec_ptp_devinfo_1((unsigned int)val[13]);
	aee_rr_rec_ptp_devinfo_2((unsigned int)val[14]);
	aee_rr_rec_ptp_devinfo_3((unsigned int)val[22]);
	aee_rr_rec_ptp_devinfo_4((unsigned int)val[16]);
	aee_rr_rec_ptp_devinfo_5((unsigned int)val[17]);
	aee_rr_rec_ptp_devinfo_6((unsigned int)val[18]);
	aee_rr_rec_ptp_devinfo_7((unsigned int)val[19]);
#endif

	for (i = 1; i < IDX_HW_RES_SN; i++) {
		if (((i == 3) || (i == 4) || (i == 7)) &&
			(val[i] == 0)) {
			ret = 1;
			safeEfuse = 1;
			eem_error("No EFUSE (val[%d]), use safe efuse\n", i);
			break;
		}
	}

	if (val[IDX_HW_RES_SN] == 0) {
		sn_safeEfuse = 1;
		eem_error("No SN EFUSE (val[%d])\n", i);
	}

#if (EEM_FAKE_EFUSE)
	eem_checkEfuse = 1;
#endif

#ifdef MC50_LOAD
	safeEfuse = 1;
#endif
	if (safeEfuse) {
		val[0] = DEVINFO_0;
		val[1] = DEVINFO_1;
		val[2] = DEVINFO_2;
		val[3] = DEVINFO_3;
		val[4] = DEVINFO_4;
		val[5] = DEVINFO_5;
		val[6] = DEVINFO_6;
		val[7] = DEVINFO_7;
		val[8] = DEVINFO_8;
		val[9] = DEVINFO_9;
		val[10] = DEVINFO_10;
		val[11] = DEVINFO_11;
		val[12] = DEVINFO_12;
		val[13] = DEVINFO_13;
		val[14] = DEVINFO_14;
		val[15] = DEVINFO_15;
		val[16] = DEVINFO_16;
		val[17] = DEVINFO_17;
	}

	if (sn_safeEfuse) {
		val[18] = DEVINFO_21;
		val[19] = DEVINFO_22;
		val[20] = DEVINFO_23;
		val[21] = DEVINFO_24;
		val[22] = DEVINFO_25;
	}

#if EN_TEST_EQUATION
	eem_devinfo.ATE_TEMP = 3;
#if 0
	eem_devinfo.T_SVT_HV_BCPU = 108;
	eem_devinfo.T_SVT_LV_BCPU = 60;
	eem_devinfo.T_SVT_HV_BCPU_RT = 110;
	eem_devinfo.T_SVT_LV_BCPU_RT = 59;
	eem_devinfo.T_SVT_HV_LCPU = 108;
	eem_devinfo.T_SVT_LV_LCPU = 60;
	eem_devinfo.T_SVT_HV_LCPU_RT = 110;
	eem_devinfo.T_SVT_LV_LCPU_RT = 59;
#endif
	val[22] = 0x3B6D3B6E;

#endif


	FUNC_EXIT(FUNC_LV_HELP);
	return ret;
}

/*============================================================
 * function declarations of EEM detectors
 *============================================================
 */
//static void mt_ptp_lock(unsigned long *flags);
//static void mt_ptp_unlock(unsigned long *flags);

/*=============================================================
 * Local function definition
 *=============================================================
 */
#ifdef CONFIG_EEM_AEE_RR_REC
static void _mt_eem_aee_init(void)
{
	aee_rr_rec_ptp_vboot(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_cpu_big_volt(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_cpu_big_volt_1(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_cpu_big_volt_2(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_cpu_big_volt_3(0xFFFFFFFFFFFFFFFF);
#if 0
	aee_rr_rec_ptp_gpu_volt(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_gpu_volt_1(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_gpu_volt_2(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_gpu_volt_3(0xFFFFFFFFFFFFFFFF);
#endif
	aee_rr_rec_ptp_cpu_little_volt(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_cpu_little_volt_1(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_cpu_little_volt_2(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_cpu_little_volt_3(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_cpu_2_little_volt(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_cpu_2_little_volt_1(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_cpu_2_little_volt_2(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_cpu_2_little_volt_3(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_cpu_cci_volt(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_cpu_cci_volt_1(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_cpu_cci_volt_2(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_cpu_cci_volt_3(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_temp(0xFFFFFFFFFFFFFFFF);
	aee_rr_rec_ptp_status(0xFF);
}
#endif

#ifdef CONFIG_THERMAL
/* common part in thermal */
int __attribute__((weak))
tscpu_get_temp_by_bank(enum thermal_bank_name ts_bank)
{
	eem_error("cannot find %s (thermal has not ready yet!)\n", __func__);
	return 0;
}

int __attribute__((weak))
tscpu_is_temp_valid(void)
{
	eem_error("cannot find %s (thermal has not ready yet!)\n", __func__);
	return 0;
}
#endif

#if 0
static struct eem_ctrl *id_to_eem_ctrl(enum eem_ctrl_id id)
{
	if (likely(id < NR_EEM_CTRL))
		return &eem_ctrls[id];
	else
		return NULL;
}
void base_ops_enable(struct eemsn_det *det, int reason)
{
	/* FIXME: UNDER CONSTRUCTION */
	FUNC_ENTER(FUNC_LV_HELP);
	det->disabled &= ~reason;
	FUNC_EXIT(FUNC_LV_HELP);
}

void base_ops_switch_bank(struct eemsn_det *det, enum eem_phase phase)
{
	unsigned int coresel;

	FUNC_ENTER(FUNC_LV_HELP);

	coresel = (eem_read(EEMCORESEL) & ~BITMASK(2:0))
		| BITS(2:0, det->det_id);

	/* 803f0000 + det->det_id = enable ctrl's swcg clock */
	/* 003f0000 + det->det_id = disable ctrl's swcg clock */
	/* bug: when system resume, need to restore coresel value */
	if (phase == EEM_PHASE_INIT01) {
		coresel |= CORESEL_VAL;
	} else {
		coresel |= CORESEL_INIT2_VAL;
#if defined(CFG_THERM_LVTS) && CFG_LVTS_DOMINATOR
		coresel &= 0x0fffffff;
#else
		coresel &= 0x0ffffeff;  /* get temp from AUXADC */
#endif
	}
	eem_write(EEMCORESEL, coresel);

	if ((eem_read(EEMCORESEL) & 0x7) != (coresel & 0x7)) {
		aee_kernel_warning("mt_eem",
		"@%s():%d, EEMCORESEL %x != %x\n",
		__func__,
		__LINE__,
		eem_read(EEMCORESEL),
		coresel);
		WARN_ON(eem_read(EEMCORESEL) != coresel);
	}

	eem_debug("[%s] 0x1100bf00=0x%x\n",
			((char *)(det->name) + 8), eem_read(EEMCORESEL));

	FUNC_EXIT(FUNC_LV_HELP);
}

void base_ops_disable_locked(struct eemsn_det *det, int reason)
{
	FUNC_ENTER(FUNC_LV_HELP);

	switch (reason) {
	case BY_MON_ERROR: /* 4 */
	case BY_INIT_ERROR: /* 2 */
		/* disable EEM */
		eem_write(EEMEN, 0x0 | SEC_MOD_SEL);

		/* Clear EEM interrupt EEMINTSTS */
		eem_write(EEMINTSTS, 0x00ffffff);
		/* fall through */

	case BY_PROCFS: /* 1 */
		det->disabled |= reason;
		eem_debug("det->disabled=%x", det->disabled);
		/* restore default DVFS table (PMIC) */
		eem_restore_eem_volt(det);
		break;

	default:
		eem_debug("det->disabled=%x\n", det->disabled);
		det->disabled &= ~BY_PROCFS;
		eem_debug("det->disabled=%x\n", det->disabled);
		eem_set_eem_volt(det);
		break;
	}

	eem_debug("Disable EEM[%s] done. reason=[%d]\n",
			det->name, det->disabled);

	FUNC_EXIT(FUNC_LV_HELP);
}

void base_ops_disable(struct eemsn_det *det, int reason)
{
	unsigned long flags;

	FUNC_ENTER(FUNC_LV_HELP);

	mt_ptp_lock(&flags);
	det->ops->switch_bank(det, NR_EEM_PHASE);
	det->ops->disable_locked(det, reason);
	mt_ptp_unlock(&flags);

	FUNC_EXIT(FUNC_LV_HELP);
}

int base_ops_init01(struct eemsn_det *det)
{
	FUNC_ENTER(FUNC_LV_HELP);

	if (unlikely(!HAS_FEATURE(det, FEA_INIT01))) {
		eem_debug("det %s has no INIT01\n", det->name);
		FUNC_EXIT(FUNC_LV_HELP);
		return -1;
	}

	det->ops->set_phase(det, EEM_PHASE_INIT01);

	FUNC_EXIT(FUNC_LV_HELP);

	return 0;
}


int base_ops_get_status(struct eemsn_det *det)
{
	int status;
	unsigned long flags;

	FUNC_ENTER(FUNC_LV_HELP);

	mt_ptp_lock(&flags);
	det->ops->switch_bank(det, NR_EEM_PHASE);
	status = (eem_read(EEMEN) != 0) ? 1 : 0;
	mt_ptp_unlock(&flags);

	FUNC_EXIT(FUNC_LV_HELP);

	return status;
}

void base_ops_dump_status(struct eemsn_det *det)
{
	unsigned int i;

	FUNC_ENTER(FUNC_LV_HELP);

	eem_isr_info("[%s]\n",			det->name);

	eem_isr_info("EEMINITEN = 0x%08X\n",	det->EEMINITEN);
	eem_isr_info("EEMMONEN = 0x%08X\n",	det->EEMMONEN);
	eem_isr_info("MDES = 0x%08X\n",		det->MDES);
	eem_isr_info("BDES = 0x%08X\n",		det->BDES);
	eem_isr_info("DCMDET = 0x%08X\n",	det->DCMDET);

	eem_isr_info("DCCONFIG = 0x%08X\n",	det->DCCONFIG);
	eem_isr_info("DCBDET = 0x%08X\n",	det->DCBDET);

	eem_isr_info("AGECONFIG = 0x%08X\n",	det->AGECONFIG);
	eem_isr_info("AGEM = 0x%08X\n",		det->AGEM);

	eem_isr_info("AGEDELTA = 0x%08X\n",	det->AGEDELTA);
	eem_isr_info("DVTFIXED = 0x%08X\n",	det->DVTFIXED);
	eem_isr_info("MTDES = 0x%08X\n",	det->MTDES);
	eem_isr_info("VCO = 0x%08X\n",		det->VCO);

	eem_isr_info("DETWINDOW = 0x%08X\n",	det->DETWINDOW);
	eem_isr_info("VMAX = 0x%08X\n",		det->VMAX);
	eem_isr_info("VMIN = 0x%08X\n",		det->VMIN);
	eem_isr_info("DTHI = 0x%08X\n",		det->DTHI);
	eem_isr_info("DTLO = 0x%08X\n",		det->DTLO);
	eem_isr_info("VBOOT = 0x%08X\n",	det->VBOOT);
	eem_isr_info("DETMAX = 0x%08X\n",	det->DETMAX);

	eem_isr_info("DCVOFFSETIN = 0x%08X\n",	det->DCVOFFSETIN);
	eem_isr_info("AGEVOFFSETIN = 0x%08X\n",	det->AGEVOFFSETIN);

	eem_isr_info("MTS = 0x%08X\n",		det->MTS);
	eem_isr_info("BTS = 0x%08X\n",		det->BTS);

	eem_isr_info("num_freq_tbl = %d\n", NR_FREQ);

	for (i = 0; i < NR_FREQ; i++)
		eem_isr_info("freq_tbl[%d] = %d\n",
				i, det->freq_tbl[i]);

	for (i = 0; i < NR_FREQ; i++)
		eem_isr_info("volt_tbl[%d] = %d\n",
				i, det->volt_tbl[i]);

	for (i = 0; i < NR_FREQ; i++)
		eem_isr_info("volt_tbl_init2[%d] = %d\n",
				i, det->volt_tbl_init2[i]);

	for (i = 0; i < NR_FREQ; i++)
		eem_isr_info("volt_tbl_pmic[%d] = %d\n", i,
				det->volt_tbl_pmic[i]);

	FUNC_EXIT(FUNC_LV_HELP);
}


void dump_register(void)
{

	eem_debug("EEM_DESCHAR = 0x%x\n", eem_read(EEM_DESCHAR));
	eem_debug("EEM_TEMPCHAR = 0x%x\n", eem_read(EEM_TEMPCHAR));
	eem_debug("EEM_DETCHAR = 0x%x\n", eem_read(EEM_DETCHAR));
	eem_debug("EEM_AGECHAR = 0x%x\n", eem_read(EEM_AGECHAR));
	eem_debug("EEM_DCCONFIG = 0x%x\n", eem_read(EEM_DCCONFIG));
	eem_debug("EEM_AGECONFIG = 0x%x\n", eem_read(EEM_AGECONFIG));
	eem_debug("EEM_FREQPCT30 = 0x%x\n", eem_read(EEM_FREQPCT30));
	eem_debug("EEM_FREQPCT74 = 0x%x\n", eem_read(EEM_FREQPCT74));
	eem_debug("EEM_LIMITVALS = 0x%x\n", eem_read(EEM_LIMITVALS));
	eem_debug("EEM_VBOOT = 0x%x\n", eem_read(EEM_VBOOT));
	eem_debug("EEM_DETWINDOW = 0x%x\n", eem_read(EEM_DETWINDOW));
	eem_debug("EEMCONFIG = 0x%x\n", eem_read(EEMCONFIG));
	eem_debug("EEM_TSCALCS = 0x%x\n", eem_read(EEM_TSCALCS));
	eem_debug("EEM_RUNCONFIG = 0x%x\n", eem_read(EEM_RUNCONFIG));
	eem_debug("EEMEN = 0x%x\n", eem_read(EEMEN));
	eem_debug("EEM_INIT2VALS = 0x%x\n", eem_read(EEM_INIT2VALS));
	eem_debug("EEM_DCVALUES = 0x%x\n", eem_read(EEM_DCVALUES));
	eem_debug("EEM_AGEVALUES = 0x%x\n", eem_read(EEM_AGEVALUES));
	eem_debug("EEM_VOP30 = 0x%x\n", eem_read(EEM_VOP30));
	eem_debug("EEM_VOP74 = 0x%x\n", eem_read(EEM_VOP74));
	eem_debug("TEMP = 0x%x\n", eem_read(TEMP));
	eem_debug("EEMINTSTS = 0x%x\n", eem_read(EEMINTSTS));
	eem_debug("EEMINTSTSRAW = 0x%x\n", eem_read(EEMINTSTSRAW));
	eem_debug("EEMINTEN = 0x%x\n", eem_read(EEMINTEN));
	eem_debug("EEM_CHKSHIFT = 0x%x\n", eem_read(EEM_CHKSHIFT));
	eem_debug("EEM_VDESIGN30 = 0x%x\n", eem_read(EEM_VDESIGN30));
	eem_debug("EEM_VDESIGN74 = 0x%x\n", eem_read(EEM_VDESIGN74));
	eem_debug("EEM_AGECOUNT = 0x%x\n", eem_read(EEM_AGECOUNT));
	eem_debug("EEM_SMSTATE0 = 0x%x\n", eem_read(EEM_SMSTATE0));
	eem_debug("EEM_SMSTATE1 = 0x%x\n", eem_read(EEM_SMSTATE1));
	eem_debug("EEM_CTL0 = 0x%x\n", eem_read(EEM_CTL0));
	eem_debug("EEMCORESEL = 0x%x\n", eem_read(EEMCORESEL));
	eem_debug("EEM_THERMINTST = 0x%x\n", eem_read(EEM_THERMINTST));
	eem_debug("EEMODINTST = 0x%x\n", eem_read(EEMODINTST));
	eem_debug("EEM_THSTAGE0ST = 0x%x\n", eem_read(EEM_THSTAGE0ST));
	eem_debug("EEM_THSTAGE1ST = 0x%x\n", eem_read(EEM_THSTAGE1ST));
	eem_debug("EEM_THSTAGE2ST = 0x%x\n", eem_read(EEM_THSTAGE2ST));
	eem_debug("EEM_THAHBST0 = 0x%x\n", eem_read(EEM_THAHBST0));
	eem_debug("EEM_THAHBST1 = 0x%x\n", eem_read(EEM_THAHBST1));
	eem_debug("EEMSPARE0 = 0x%x\n", eem_read(EEMSPARE0));
	eem_debug("EEMSPARE1 = 0x%x\n", eem_read(EEMSPARE1));
	eem_debug("EEMSPARE2 = 0x%x\n", eem_read(EEMSPARE2));
	eem_debug("EEMSPARE3 = 0x%x\n", eem_read(EEMSPARE3));
	eem_debug("EEM_THSLPEVEB = 0x%x\n", eem_read(EEM_THSLPEVEB));
	eem_debug("EEM_TEMP = 0x%x\n", eem_read(TEMP));
	eem_debug("EEM_THERMAL = 0x%x\n", eem_read(EEM_THERMAL));

}

void base_ops_set_phase(struct eemsn_det *det, enum eem_phase phase)
{
	unsigned int i, filter, val;

	FUNC_ENTER(FUNC_LV_HELP);
	det->ops->switch_bank(det, phase);

	/* config EEM register */
	eem_write(EEM_DESCHAR,
		  ((det->BDES << 8) & 0xff00) | (det->MDES & 0xff));
	eem_write(EEM_TEMPCHAR,
		  (((det->VCO << 16) & 0xff0000) |
		   ((det->MTDES << 8) & 0xff00) | (det->DVTFIXED & 0xff)));
	eem_write(EEM_DETCHAR,
		  ((det->DCBDET << 8) & 0xff00) | (det->DCMDET & 0xff));

	eem_write(EEM_DCCONFIG, det->DCCONFIG);
	eem_write(EEM_AGECONFIG, det->AGECONFIG);

	if (phase == EEM_PHASE_MON)
		eem_write(EEM_TSCALCS,
			  ((det->BTS << 12) & 0xfff000) | (det->MTS & 0xfff));

	if (det->AGEM == 0x0)
		eem_write(EEM_RUNCONFIG, 0x80000000);
	else {
		val = 0x0;

		for (i = 0; i < 24; i += 2) {
			filter = 0x3 << i;

			if (((det->AGECONFIG) & filter) == 0x0)
				val |= (0x1 << i);
			else
				val |= ((det->AGECONFIG) & filter);
		}
		eem_write(EEM_RUNCONFIG, val);
	}

	eem_fill_freq_table(det);

	eem_write(EEM_LIMITVALS,
		  ((det->VMAX << 24) & 0xff000000)	|
		  ((det->VMIN << 16) & 0xff0000)	|
		  ((det->DTHI << 8) & 0xff00)		|
		  (det->DTLO & 0xff));
	/* eem_write(EEM_LIMITVALS, 0xFF0001FE); */
	eem_write(EEM_VBOOT, (((det->VBOOT) & 0xff)));
	eem_write(EEM_DETWINDOW, (((det->DETWINDOW) & 0xffff)));
	eem_write(EEMCONFIG, (((det->DETMAX) & 0xffff)));


	eem_write(EEM_CHKSHIFT, 0x87);
	if (eem_read(EEM_CHKSHIFT) != 0x87) {
		aee_kernel_warning("mt_eem",
		"@%s():%d, EEM_CHKSHIFT %x\n",
		__func__,
		__LINE__,
		eem_read(EEM_CHKSHIFT));
		WARN_ON(eem_read(EEM_CHKSHIFT) != 0x87);
	}

	/* eem ctrl choose thermal sensors */
	eem_write(EEM_CTL0, det->EEMCTL0);
	/* clear all pending EEM interrupt & config EEMINTEN */
	eem_write(EEMINTSTS, 0xffffffff);

	/* work around set thermal register
	 * eem_write(EEM_THERMAL, 0x200);
	 */

	eem_debug(" %s set phase = %d\n", ((char *)(det->name) + 8), phase);
	switch (phase) {
	case EEM_PHASE_INIT01:
		eem_debug("EEM_SET_PHASE01\n ");
		eem_write(EEMINTEN, 0x00005f01);
		/* enable EEM INIT measurement */
		eem_write(EEMEN, 0x00000001 | SEC_MOD_SEL);
		if (eem_read(EEMEN) != (0x00000001 | SEC_MOD_SEL)) {
			aee_kernel_warning("mt_eem",
			"@%s():%d, EEMEN %x\n",
			__func__,
			__LINE__,
			eem_read(EEMEN));
			WARN_ON(eem_read(EEMEN) != (0x00000001 | SEC_MOD_SEL));
		}
		eem_debug("EEMEN = 0x%x, EEMINTEN = 0x%x\n",
				eem_read(EEMEN), eem_read(EEMINTEN));
		dump_register();
		udelay(250); /* all banks' phase cannot be set without delay */
		break;

	case EEM_PHASE_INIT02:
		/* check if DCVALUES is minus and set DCVOFFSETIN to zero */
		if ((det->DCVOFFSETIN & 0x8000) || (eem_devinfo.FT_PGM == 0))
			det->DCVOFFSETIN = 0;

#if ENABLE_MINIHQA
		det->DCVOFFSETIN = 0;
#endif

		eem_debug("EEM_SET_PHASE02\n ");
		eem_write(EEMINTEN, 0x00005f01);

		eem_write(EEM_INIT2VALS,
			  ((det->AGEVOFFSETIN << 16) & 0xffff0000) |
			  (det->DCVOFFSETIN & 0xffff));

		/* enable EEM INIT measurement */
		eem_write(EEMEN, 0x00000005 | SEC_MOD_SEL);
		if (eem_read(EEMEN) != (0x00000005 | SEC_MOD_SEL)) {
			aee_kernel_warning("mt_eem",
			"@%s():%d, EEMEN %x\n",
			__func__,
			__LINE__,
			eem_read(EEMEN));
			WARN_ON(eem_read(EEMEN) != (0x00000005 | SEC_MOD_SEL));
		}

		dump_register();
		udelay(200); /* all banks' phase cannot be set without delay */
		break;

	case EEM_PHASE_MON:
		eem_debug("EEM_SET_PHASE_MON\n ");
		eem_write(EEMINTEN, 0x00FF0000);
		/* enable EEM monitor mode */
		eem_write(EEMEN, 0x00000002 | SEC_MOD_SEL);
		/* dump_register(); */
		break;

	default:
		WARN_ON(1); /*BUG()*/
		break;
	}
	/* mt_ptp_unlock(&flags); */

	FUNC_EXIT(FUNC_LV_HELP);
}
#endif
int base_ops_get_temp(struct eemsn_det *det)
{
#ifdef CONFIG_THERMAL
	enum thermal_bank_name ts_bank;

	if (det_to_id(det) == EEMSN_DET_L)
		ts_bank = THERMAL_BANK2;
	else if (det_to_id(det) == EEMSN_DET_B)
		ts_bank = THERMAL_BANK0;
	else if (det_to_id(det) == EEMSN_DET_CCI)
		ts_bank = THERMAL_BANK2;

	else
		ts_bank = THERMAL_BANK0;

	return tscpu_get_temp_by_bank(ts_bank);
#else
	return 0;
#endif
}
#if 0
int base_ops_get_volt(struct eemsn_det *det)
{
	FUNC_ENTER(FUNC_LV_HELP);
	eem_debug("[%s] default func\n", __func__);
	FUNC_EXIT(FUNC_LV_HELP);

	return 0;
}

int base_ops_set_volt(struct eemsn_det *det)
{
	FUNC_ENTER(FUNC_LV_HELP);
	eem_debug("[%s] default func\n", __func__);
	FUNC_EXIT(FUNC_LV_HELP);

	return 0;
}

void base_ops_restore_default_volt(struct eemsn_det *det)
{
	FUNC_ENTER(FUNC_LV_HELP);
	eem_debug("[%s] default func\n", __func__);
	FUNC_EXIT(FUNC_LV_HELP);
}

void base_ops_get_freq_table(struct eemsn_det *det)
{
	FUNC_ENTER(FUNC_LV_HELP);

	det->freq_tbl[0] = 100;
	NR_FREQ = 1;

	FUNC_EXIT(FUNC_LV_HELP);
}

void base_ops_get_orig_volt_table(struct eemsn_det *det)
{
	FUNC_ENTER(FUNC_LV_HELP);
	FUNC_EXIT(FUNC_LV_HELP);
}

static long long eem_get_current_time_us(void)
{
	struct timeval t;

	do_gettimeofday(&t);
	return((t.tv_sec & 0xFFF) * 1000000 + t.tv_usec);
}

/*=============================================================
 * Global function definition
 *=============================================================
 */
static void mt_ptp_lock(unsigned long *flags)
{
	spin_lock_irqsave(&eem_spinlock, *flags);
	eem_pTime_us = eem_get_current_time_us();

}
EXPORT_SYMBOL(mt_ptp_lock);

static void mt_ptp_unlock(unsigned long *flags)
{
	eem_cTime_us = eem_get_current_time_us();
	EEM_IS_TOO_LONG();
	spin_unlock_irqrestore(&eem_spinlock, *flags);
}
EXPORT_SYMBOL(mt_ptp_unlock);

void mt_record_lock(unsigned long *flags)
{
	spin_lock_irqsave(&record_spinlock, *flags);
}
EXPORT_SYMBOL(mt_record_lock);

void mt_record_unlock(unsigned long *flags)
{
	spin_unlock_irqrestore(&record_spinlock, *flags);
}
EXPORT_SYMBOL(mt_record_unlock);

/*
 * timer for log
 */
static enum hrtimer_restart eem_log_timer_func(struct hrtimer *timer)
{
	struct eemsn_det *det;

	FUNC_ENTER(FUNC_LV_HELP);

	for_each_det(det) {
		/* get rid of redundent banks */
		if (det->features == 0)
			continue;

		eem_debug("Timer Bk=%d (%d)(%d, %d, %d, %d, %d, %d, %d, %d)(0x%x)\n",
			det->det_id,
			det->ops->get_temp(det),
			det->ops->pmic_2_volt(det, det->volt_tbl_pmic[0]),
			det->ops->pmic_2_volt(det, det->volt_tbl_pmic[1]),
			det->ops->pmic_2_volt(det, det->volt_tbl_pmic[2]),
			det->ops->pmic_2_volt(det, det->volt_tbl_pmic[3]),
			det->ops->pmic_2_volt(det, det->volt_tbl_pmic[4]),
			det->ops->pmic_2_volt(det, det->volt_tbl_pmic[5]),
			det->ops->pmic_2_volt(det, det->volt_tbl_pmic[6]),
			det->ops->pmic_2_volt(det, det->volt_tbl_pmic[7]),
			det->t250);

#if 0
		det->freq_tbl[0],
		det->freq_tbl[1],
		det->freq_tbl[2],
		det->freq_tbl[3],
		det->freq_tbl[4],
		det->freq_tbl[5],
		det->freq_tbl[6],
		det->freq_tbl[7],
		det->dcvalues[3],
		det->freqpct30[3],
		det->eem_26c[3],
		det->vop30[3]
#endif
	}

	hrtimer_forward_now(timer, ns_to_ktime(LOG_INTERVAL));
	FUNC_EXIT(FUNC_LV_HELP);

	return HRTIMER_RESTART;
}

static void eem_calculate_aging_margin(struct eemsn_det *det,
	int start_oft, int end_oft)
{

	int num_bank_freq, offset, i = 0;


	num_bank_freq = NR_FREQ;

	offset = start_oft - end_oft;
	for (i = 0; i < NR_FREQ; i++) {
		if (i == 0)
			det->volt_aging[i] = start_oft;
		else
			det->volt_aging[i] = start_oft -
			((offset * i) / NR_FREQ);
	}

}
#endif
#if UPDATE_TO_UPOWER
static void eem_save_final_volt_aee(struct eemsn_det *ndet)
{
#ifdef CONFIG_EEM_AEE_RR_REC
	int i;

	if (ndet == NULL)
		return;

	for (i = 0; i < NR_FREQ; i++) {
		switch (ndet->det_id) {
		case EEMSN_DET_L:
			if (i < 8) {
				aee_rr_rec_ptp_cpu_little_volt_2(
				((unsigned long long)(ndet->volt_tbl_pmic[i])
				<< (8 * i)) |
				(aee_rr_curr_ptp_cpu_little_volt_2() & ~
				((unsigned long long)(0xFF) << (8 * i)))
				);
			} else {
				aee_rr_rec_ptp_cpu_little_volt_3(
				((unsigned long long)(ndet->volt_tbl_pmic[i])
				 << (8 * (i - 8))) |
				(aee_rr_curr_ptp_cpu_little_volt_3() & ~
					((unsigned long long)(0xFF)
					<< (8 * (i - 8))))
				);
			}
			break;

		case EEMSN_DET_B:
			if (i < 8) {
				aee_rr_rec_ptp_cpu_big_volt_2(
					((unsigned long long)
					(ndet->volt_tbl_pmic[i])
					 << (8 * i)) |
					(aee_rr_curr_ptp_cpu_big_volt_2() & ~
						((unsigned long long)(0xFF)
						 << (8 * i))
					)
				);
			} else {
				aee_rr_rec_ptp_cpu_big_volt_3(
					((unsigned long long)
					(ndet->volt_tbl_pmic[i])
					 << (8 * (i - 8))) |
					(aee_rr_curr_ptp_cpu_big_volt_3() & ~
						((unsigned long long)(0xFF)
						 << (8 * (i - 8)))
					)
				);
			}
			break;
		case EEMSN_DET_CCI:
			if (i < 8) {
				aee_rr_rec_ptp_cpu_cci_volt_2(
					((unsigned long long)
					(ndet->volt_tbl_pmic[i])
					 << (8 * i)) |
					(aee_rr_curr_ptp_cpu_cci_volt_2() & ~
						((unsigned long long)(0xFF)
						 << (8 * i))
					)
				);
			} else {
				aee_rr_rec_ptp_cpu_cci_volt_3(
					((unsigned long long)
					(ndet->volt_tbl_pmic[i])
					 << (8 * (i - 8))) |
					(aee_rr_curr_ptp_cpu_cci_volt_3() & ~
						((unsigned long long)(0xFF)
						 << (8 * (i - 8)))
					)
				);
			}
			break;
		default:
			break;
		}
	}
#endif
}
#endif

static void eem_save_init2_volt_aee(struct eemsn_det *ndet)
{
#ifdef CONFIG_EEM_AEE_RR_REC
	int i;

	if (ndet == NULL)
		return;

	for (i = 0; i < NR_FREQ; i++) {
		switch (ndet->det_id) {
		case EEMSN_DET_L:
			if (i < 8) {
				aee_rr_rec_ptp_cpu_little_volt(
				((unsigned long long)(ndet->volt_tbl_init2[i])
				<< (8 * i)) |
				(aee_rr_curr_ptp_cpu_little_volt() & ~
				((unsigned long long)(0xFF) << (8 * i)))
				);
			} else {
				aee_rr_rec_ptp_cpu_little_volt_1(
				((unsigned long long)(ndet->volt_tbl_init2[i])
				 << (8 * (i - 8))) |
				(aee_rr_curr_ptp_cpu_little_volt_1() & ~
					((unsigned long long)(0xFF)
					<< (8 * (i - 8))))
				);
			}
			break;

		case EEMSN_DET_B:
			if (i < 8) {
				aee_rr_rec_ptp_cpu_big_volt(
				((unsigned long long)(ndet->volt_tbl_init2[i])
					 << (8 * i)) |
					(aee_rr_curr_ptp_cpu_big_volt() & ~
						((unsigned long long)(0xFF)
						 << (8 * i))
					)
				);
			} else {
				aee_rr_rec_ptp_cpu_big_volt_1(
				((unsigned long long)(ndet->volt_tbl_init2[i])
					 << (8 * (i - 8))) |
					(aee_rr_curr_ptp_cpu_big_volt_1() & ~
						((unsigned long long)(0xFF)
						 << (8 * (i - 8)))
					)
				);
			}
			break;
		case EEMSN_DET_CCI:
			if (i < 8) {
				aee_rr_rec_ptp_cpu_cci_volt(
				((unsigned long long)(ndet->volt_tbl_init2[i])
					 << (8 * i)) |
					(aee_rr_curr_ptp_cpu_cci_volt() & ~
						((unsigned long long)(0xFF)
						 << (8 * i))
					)
				);
			} else {
				aee_rr_rec_ptp_cpu_cci_volt_1(
				((unsigned long long)(ndet->volt_tbl_init2[i])
					 << (8 * (i - 8))) |
					(aee_rr_curr_ptp_cpu_cci_volt_1() & ~
						((unsigned long long)(0xFF)
						 << (8 * (i - 8)))
					)
				);
			}
			break;
		default:
			break;
		}
	}
#endif
}

#if 0
static void eem_save_sn_cal_aee(void)
{
#ifdef CONFIG_EEM_AEE_RR_REC
	int i;

	i = index;

	if (ndet == NULL)
		return;

	switch (ndet->det_id) {
	case EEMSN_DET_L:
		if (i < 8) {
			aee_rr_rec_ptp_cpu_2_little_volt(
			((unsigned long long)(ndet->volt_tbl_init2[i])
			<< (8 * i)) |
			(aee_rr_rec_ptp_cpu_2_little_volt() & ~
			((unsigned long long)(0xFF) << (8 * i)))
			);
		} else {
			aee_rr_rec_ptp_cpu_2_little_volt_1(
			((unsigned long long)(ndet->volt_tbl_init2[i])
			 << (8 * (i - 8))) |
			(aee_rr_rec_ptp_cpu_2_little_volt_1() & ~
				((unsigned long long)(0xFF)
				<< (8 * (i - 8))))
			);
		}
		break;

	case EEMSN_DET_B:
		if (i < 8) {
			aee_rr_rec_ptp_cpu_2_little_volt_2(
				((unsigned long long)(ndet->volt_tbl_init2[i])
				 << (8 * i)) |
				(aee_rr_rec_ptp_cpu_2_little_volt_2() & ~
					((unsigned long long)(0xFF)
					 << (8 * i))
				)
			);
		} else {
			aee_rr_rec_ptp_cpu_2_little_volt_3(
				((unsigned long long)(ndet->volt_tbl_init2[i])
				 << (8 * (i - 8))) |
				(aee_rr_rec_ptp_cpu_2_little_volt_3() & ~
					((unsigned long long)(0xFF)
					 << (8 * (i - 8)))
				)
			);
		}
		break;

	default:
		break;
	}

#endif
}
#endif

static void inherit_base_det(struct eemsn_det *det)
{
	/*
	 * Inherit ops from EEMSN_DET_base_ops if ops in det is NULL
	 */
	FUNC_ENTER(FUNC_LV_HELP);

	#define INIT_OP(ops, func)					\
		do {							\
			if (ops->func == NULL)				\
				ops->func = eem_det_base_ops.func;	\
		} while (0)
#if 0
	INIT_OP(det->ops, disable);
	INIT_OP(det->ops, disable_locked);
	INIT_OP(det->ops, switch_bank);
	INIT_OP(det->ops, init01);
	INIT_OP(det->ops, init02);
	INIT_OP(det->ops, mon_mode);
	INIT_OP(det->ops, get_status);
	INIT_OP(det->ops, dump_status);
	INIT_OP(det->ops, set_phase);
	INIT_OP(det->ops, get_volt);
	INIT_OP(det->ops, set_volt);
	INIT_OP(det->ops, restore_default_volt);
	INIT_OP(det->ops, get_freq_table);
	INIT_OP(det->ops, get_orig_volt_table);
#endif
	INIT_OP(det->ops, get_temp);

	INIT_OP(det->ops, volt_2_pmic);
	INIT_OP(det->ops, volt_2_eem);
	INIT_OP(det->ops, pmic_2_volt);
	INIT_OP(det->ops, eem_2_pmic);

	FUNC_EXIT(FUNC_LV_HELP);
}

#if UPDATE_TO_UPOWER
static enum upower_bank transfer_ptp_to_upower_bank(unsigned int det_id)
{
	enum upower_bank bank;

	switch (det_id) {
	case EEMSN_DET_L:
		bank = UPOWER_BANK_LL;
		break;
	case EEMSN_DET_B:
		bank = UPOWER_BANK_L;
		break;
	case EEMSN_DET_CCI:
		bank = UPOWER_BANK_CCI;
		break;
	default:
		bank = NR_UPOWER_BANK;
		break;
	}
	return bank;
}

static void eem_update_init2_volt_to_upower
	(struct eemsn_det *det, unsigned int *pmic_volt)
{
	unsigned int volt_tbl[NR_FREQ_CPU];
	enum upower_bank bank;
	int i;

	for (i = 0; i < NR_FREQ; i++)
		volt_tbl[i] = det->ops->pmic_2_volt(det, pmic_volt[i]);

	bank = transfer_ptp_to_upower_bank(det_to_id(det));
#if 1
	if (bank < NR_UPOWER_BANK) {
		upower_update_volt_by_eem(bank, volt_tbl, NR_FREQ);
		 eem_debug
		  ("volt to upower (id: %d upower, pmic_volt[0] :0x%x)\n",
		  det->det_id, pmic_volt[0]);
	}
#endif
}

#endif

#if EN_EEM
#if SUPPORT_DCONFIG
static void eem_dconfig_set_det(struct eemsn_det *det, struct device_node *node)
{
	enum eemsn_det_id det_id = det_to_id(det);
#if ENABLE_LOO
	struct eemsn_det *highdet;
#endif
	int doe_initmon = 0xFF, doe_clamp = 0;
	int doe_offset = 0xFF;
	int rc1 = 0, rc2 = 0, rc3 = 0;
#if UPDATE_TO_UPOWER
	int i;
#endif

#if ENABLE_GPU
	if (det_id > EEMSN_DET_GPU)
		return;
#endif

	switch (det_id) {
	case EEMSN_DET_L:
		rc1 = of_property_read_u32(node, "eem-initmon-little",
			&doe_initmon);
		rc2 = of_property_read_u32(node, "eem-clamp-little",
			&doe_clamp);
		rc3 = of_property_read_u32(node, "eem-offset-little",
			&doe_offset);
		break;
	case EEMSN_DET_B:
		rc1 = of_property_read_u32(node, "eem-initmon-big",
			&doe_initmon);
		rc2 = of_property_read_u32(node, "eem-clamp-big",
			&doe_clamp);
		rc3 = of_property_read_u32(node, "eem-offset-big",
			&doe_offset);
		break;
	case EEMSN_DET_CCI:
		rc1 = of_property_read_u32(node, "eem-initmon-cci",
			&doe_initmon);
		rc2 = of_property_read_u32(node, "eem-clamp-cci",
			&doe_clamp);
		rc3 = of_property_read_u32(node, "eem-offset-cci",
			&doe_offset);
		break;
#if ENABLE_GPU
	case EEMSN_DET_GPU:
		rc1 = of_property_read_u32(node, "eem-initmon-gpu",
			&doe_initmon);
		rc2 = of_property_read_u32(node, "eem-clamp-gpu",
			&doe_clamp);
		rc3 = of_property_read_u32(node, "eem-offset-gpu",
			&doe_offset);
		break;
#endif
	default:
		eem_debug("[%s]: Unknown det_id %d\n", __func__, det_id);
		break;
	}
#if 0
	eem_error("[DCONFIG] det_id:%d, feature modified by DT(0x%x)\n",
			det_id, doe_initmon);
	eem_error("[DCONFIG] doe_offset:%x, doe_clamp:%x\n",
			doe_offset, doe_clamp);
#endif
	if ((!rc1) && (doe_initmon != 0xFF)) {
		if (det->features != doe_initmon) {
			det->features = doe_initmon;
			eemsn_log->det_log[det->det_id].features =
				(unsigned char)det->features;
			eem_error("[DCONFIG] feature modified by DT(0x%x)\n",
				doe_initmon);

			if (det_id < NR_EEMSN_DET) {
#if UPDATE_TO_UPOWER
				if (HAS_FEATURE(det, FEA_INIT01) == 0) {
					for (i = 0; i < NR_FREQ; i++)
						record_tbl_locked[i] =
					(unsigned int)det->volt_tbl_orig[i];

					eem_update_init2_volt_to_upower
					(det, record_tbl_locked);
				}
#endif
			}
		}
	}

	if (!rc2)
		det->volt_clamp = (char)doe_clamp;

	if ((!rc3) && (doe_offset != 0xFF)) {
		if (doe_offset < 1000)
			det->volt_offset = (char)(doe_offset & 0xff);
		else
			det->volt_offset = 0 -
				(char)((doe_offset - 1000) & 0xff);

		eemsn_log->det_log[det->det_id].volt_offset =
			det->volt_offset;
	}



}
#endif


static int eem_probe(struct platform_device *pdev)
{
	unsigned int ret;
	struct eemsn_det *det;
	struct eem_ipi_data eem_data;
#ifdef CONFIG_OF
	struct device_node *node = NULL;
#endif
#if UPDATE_TO_UPOWER
	unsigned int locklimit = 0;
	//unsigned char lock;
	unsigned int i;
#endif

#if SUPPORT_DCONFIG
	unsigned int doe_status, sn_doe_status;
#endif
	enum mt_cpu_dvfs_id cpudvfsindex;

	FUNC_ENTER(FUNC_LV_MODULE);
	seq = 0;

#ifdef CONFIG_OF
	node = pdev->dev.of_node;
	if (!node) {
		eem_error("get eem device node err\n");
		return -ENODEV;
	}

#if SUPPORT_DCONFIG
	if (of_property_read_u32(node, "eem-status",
		&doe_status) < 0) {
		eem_debug("[DCONFIG] eem-status read error!\n");
	} else {
		eem_debug("[DCONFIG] success-> status:%d, EEM_Enable:%d\n",
			doe_status, ctrl_EEMSN_Enable);
		if (((doe_status == 1) || (doe_status == 0)) &&
			(ctrl_EEMSN_Enable != (unsigned char)doe_status)) {
			ctrl_EEMSN_Enable = (unsigned char)doe_status;
			eem_error("[DCONFIG] eem sts modified by DT(0x%x).\n",
				doe_status);
		}
	}
	if (of_property_read_u32(node, "sn-status",
		&sn_doe_status) < 0) {
		eem_debug("[DCONFIG] sn-status read error!\n");
	} else {
		eem_debug("[DCONFIG] success-> status:%d, sn_Enable:%d\n",
			sn_doe_status, ctrl_SN_Enable);
		if (((sn_doe_status == 1) || (sn_doe_status == 0)) &&
			(ctrl_SN_Enable != (unsigned char)sn_doe_status)) {
			ctrl_SN_Enable = (unsigned char)sn_doe_status;
			eem_error("[DCONFIG] sn sts modified by DT(0x%x).\n",
				sn_doe_status);
		}
	}

#endif

#if SUPPORT_PICACHU
	/* Setup IO addresses */
	eem_base = of_iomap(node, 0);
	eem_debug("[EEM] eem_base = 0x%p\n", eem_base);
#if 0
	eem_irq_number = irq_of_parse_and_map(node, 0);
	eem_debug("[THERM_CTRL] eem_irq_number=%d\n", eem_irq_number);
	if (!eem_irq_number) {
		eem_error("[EEM] get irqnr failed=0x%x\n", eem_irq_number);
		return 0;
	}
	/* infra_ao */
	node_infra = of_find_compatible_node(NULL, NULL, INFRA_AO_NODE);
	if (!node_infra) {
		eem_debug("INFRA_AO_NODE Not Found\n");
		return 0;
	}

	infra_base = of_iomap(node_infra, 0);
	if (!infra_base) {
		eem_debug("infra_ao Map Failed\n");
		return 0;
	}
#endif
#endif
#endif


#if 0
	/* set EEM IRQ */
	ret = request_irq(eem_irq_number, eem_isr,
			IRQF_TRIGGER_HIGH, "eem", NULL);
	if (ret) {
		eem_error("EEM IRQ register failed (%d)\n", ret);
		WARN_ON(1);
	}
	eem_debug("Set EEM IRQ OK.\n");
#endif
#if SUPPORT_PICACHU
#ifndef MC50_LOAD
	get_picachu_efuse();
#endif
#endif

#ifdef CONFIG_EEM_AEE_RR_REC
	_mt_eem_aee_init();
#endif


	for_each_det(det)
		inherit_base_det(det);

	/* for slow idle */
	ptp_data[0] = 0xffffffff;
#if 0
	for_each_det(det)
		eem_init_det(det, &eem_devinfo);

	/* get original volt from cpu dvfs before init01*/
	for_each_det(det) {
#if 0
		if (det->ops->get_freq_table) {
			det->ops->get_freq_table(det);
			memcpy(eemsn_log->det_log[det->det_id].freq_tbl,
				det->freq_tbl, sizeof(det->freq_tbl));
			eemsn_log->det_log[det->det_id].turn_pt =
				det->turn_pt;
		}
#endif
		if (det->ops->get_orig_volt_table) {
			det->ops->get_orig_volt_table(det);
			memcpy(eemsn_log->det_log[det->det_id].volt_tbl_orig,
				det->volt_tbl_orig, sizeof(det->volt_tbl_orig));
		}
	}
	memset(&eem_data, 0, sizeof(struct eem_ipi_data));
	eem_data.u.data.arg[0] = 0;
	ret = eem_to_cputoeb(IPI_EEMSN_PROBE, &eem_data);
#endif
#if SUPPORT_DCONFIG
	for_each_det(det)
		eem_dconfig_set_det(det, node);
#endif

#ifdef EEM_NOT_READY
	ctrl_EEMSN_Enable = 0;
	ctrl_SN_Enable = 0;
#endif

	for_each_det(det) {
		if ((det->num_freq_tbl < 8) ||
			(det->volt_tbl_orig[0] == 0) ||
			(det->freq_tbl[0] == 0)) {
			ctrl_EEMSN_Enable = 0;
			ctrl_SN_Enable = 0;
		}
	}

	eemsn_log->eemsn_enable = ctrl_EEMSN_Enable;
	eemsn_log->sn_enable = ctrl_SN_Enable;

#if 1
	memset(&eem_data, 0, sizeof(struct eem_ipi_data));
	eem_data.u.data.arg[0] = 0;
	ret = eem_to_cputoeb(IPI_EEMSN_GET_EEM_VOLT, &eem_data);
#endif
	ptp_data[0] = 0;

	if (ctrl_EEMSN_Enable != 0) {
#if UPDATE_TO_UPOWER
		while (1) {
			if ((eemsn_log->init2_v_ready == 0) &&
				(locklimit < 5)) {
				locklimit++;
				eem_error(
		"wait init2_v_ready:%d, locklimit:%d\n",
				eemsn_log->init2_v_ready, locklimit);
				mdelay(5); /* wait 5 ms */
				continue; /* if lock, read dram again */
			} else
				break;
		}
		for_each_det(det) {
			if (eemsn_log->init2_v_ready == 0)
				for (i = 0; i < NR_FREQ; i++)
					det->volt_tbl_pmic[i] = (unsigned int)
						det->volt_tbl_orig[i];
			else {
				for (i = 0; i < NR_FREQ; i++) {
					if (
					eemsn_log->det_log[
	det->det_id].volt_tbl_pmic[i] != 0)
						det->volt_tbl_pmic[i] =
	(unsigned int) eemsn_log->det_log[det->det_id].volt_tbl_pmic[i];
					eem_debug("pmic[%d], 0x%x",
						i, det->volt_tbl_pmic[i]);
				}
			}
			eem_update_init2_volt_to_upower(det,
				det->volt_tbl_pmic);
			eem_save_final_volt_aee(det);
		}
	} else {
		for_each_det(det) {
			for (i = 0; i < NR_FREQ; i++)
				det->volt_tbl_pmic[i] =	(unsigned int)
					det->volt_tbl_orig[i];
			eem_update_init2_volt_to_upower(det,
							det->volt_tbl_pmic);
		}
#endif
	}

	memset(&eem_data, 0, sizeof(struct eem_ipi_data));
	eem_data.u.data.arg[0] = 0;
	ret = eem_to_cputoeb(IPI_EEMSN_INIT02, &eem_data);

	if (ctrl_EEMSN_Enable == 0)
		return 0;

	informEEMisReady = 1;

	for_each_det(det) {
		cpudvfsindex = detid_to_dvfsid(det);
		mt_cpufreq_update_legacy_volt(cpudvfsindex,
			det->volt_tbl_pmic, det->num_freq_tbl);
		memcpy(det->volt_tbl_init2,
			eemsn_log->det_log[det->det_id].volt_tbl_init2,
			sizeof(det->volt_tbl_init2));
		eem_save_init2_volt_aee(det);
	}

	create_procfs();

	eem_debug("%s ok\n", __func__);
	FUNC_EXIT(FUNC_LV_MODULE);
	return 0;
}


#ifdef CONFIG_OF
static const struct of_device_id mt_eem_of_match[] = {
	{ .compatible = "mediatek,eem_fsm", },
	{},
};
#endif

static struct platform_driver eem_driver = {
	.remove	 = NULL,
	.shutdown   = NULL,
	.probe	  = eem_probe,
	.suspend	= NULL,
	.resume	 = NULL,
	.driver	 = {
	.name   = "mt-eem",
#ifdef CONFIG_OF
	.of_match_table = mt_eem_of_match,
#endif
	},
};

#ifdef CONFIG_PROC_FS
int mt_eem_opp_num(enum eemsn_det_id id)
{
	struct eemsn_det *det = id_to_eem_det(id);

	FUNC_ENTER(FUNC_LV_API);
	if (det == NULL)
		return 0;

	FUNC_EXIT(FUNC_LV_API);

	return NR_FREQ;
}
EXPORT_SYMBOL(mt_eem_opp_num);

void mt_eem_opp_freq(enum eemsn_det_id id, unsigned int *freq)
{
	struct eemsn_det *det = id_to_eem_det(id);
	int i = 0;

	FUNC_ENTER(FUNC_LV_API);

	if (det == NULL)
		return;

	for (i = 0; i < NR_FREQ; i++)
		freq[i] = det->freq_tbl[i];

	FUNC_EXIT(FUNC_LV_API);
}
EXPORT_SYMBOL(mt_eem_opp_freq);

#if 0
void mt_eem_opp_status(enum eemsn_det_id id, unsigned int *temp,
	unsigned int *volt)
{
	struct eemsn_det *det = id_to_eem_det(id);
	int i = 0;

	FUNC_ENTER(FUNC_LV_API);

#ifdef CONFIG_THERMAL
	if (id == EEMSN_DET_L)
		*temp = tscpu_get_temp_by_bank(THERMAL_BANK2);
	else if (id == EEMSN_DET_B)
		*temp = tscpu_get_temp_by_bank(THERMAL_BANK0);
	else if (id == EEMSN_DET_CCI)
		*temp = tscpu_get_temp_by_bank(THERMAL_BANK2);
#if ENABLE_GPU
	else if (id == EEMSN_DET_GPU)
		*temp = tscpu_get_temp_by_bank(THERMAL_BANK4);
#if ENABLE_LOO_G
	else if (id == EEMSN_DET_GPU_HI)
		*temp = tscpu_get_temp_by_bank(THERMAL_BANK4);
#endif
#endif
#if ENABLE_MDLA
	else if (id == EEMSN_DET_MDLA)
		*temp = tscpu_get_temp_by_bank(THERMAL_BANK3);
#endif
#if ENABLE_VPU
	else if (id == EEMSN_DET_VPU)
		*temp = tscpu_get_temp_by_bank(THERMAL_BANK3);
#endif
#if ENABLE_LOO_B
	else if (id == EEMSN_DET_B_HI)
		*temp = tscpu_get_temp_by_bank(THERMAL_BANK0);
#endif

	else
		*temp = tscpu_get_temp_by_bank(THERMAL_BANK0);
#else
	*temp = 0;
#endif

	if (det == NULL)
		return;

	for (i = 0; i < NR_FREQ; i++)
		volt[i] = det->ops->pmic_2_volt(det, det->volt_tbl_pmic[i]);

	FUNC_EXIT(FUNC_LV_API);
}
EXPORT_SYMBOL(mt_eem_opp_status);

/***************************
 * return current EEM stauts
 ***************************
 */
int mt_eem_status(enum eemsn_det_id id)
{
	struct eemsn_det *det = id_to_eem_det(id);

	FUNC_ENTER(FUNC_LV_API);
	if (det == NULL)
		return 0;
	else if (det->ops == NULL)
		return 0;
	else if (det->ops->get_status == NULL)
		return 0;

	FUNC_EXIT(FUNC_LV_API);

	return det->ops->get_status(det);
}
#endif
/**
 * ===============================================
 * PROCFS interface for debugging
 * ===============================================
 */

/*
 * show current EEM stauts
 */
static int eem_debug_proc_show(struct seq_file *m, void *v)
{
	struct eemsn_det *det = (struct eemsn_det *)m->private;

	FUNC_ENTER(FUNC_LV_HELP);

	/* FIXME: EEMEN sometimes is disabled temp */
	seq_printf(m, "[%s] %s\n",
		((char *)(det->name) + 8),
		det->disabled ? "disabled" : "enable"
		);

	FUNC_EXIT(FUNC_LV_HELP);

	return 0;
}

/*
 * set EEM status by procfs interface
 */
static ssize_t eem_debug_proc_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *pos)
{
	int ret;
	int enabled = 0;
	char *buf = (char *) __get_free_page(GFP_USER);
	struct eemsn_det *det = (struct eemsn_det *)PDE_DATA(file_inode(file));
	struct eem_ipi_data eem_data;
	int ipi_ret = 0;

	FUNC_ENTER(FUNC_LV_HELP);

	if (!buf) {
		FUNC_EXIT(FUNC_LV_HELP);
		return -ENOMEM;
	}

	ret = -EINVAL;

	if (count >= PAGE_SIZE)
		goto out;

	ret = -EFAULT;

	if (copy_from_user(buf, buffer, count))
		goto out;
	eem_debug("in eem debug proc write 2~~~~~~~~\n");

	buf[count] = '\0';

	if (!kstrtoint(buf, 10, &enabled)) {
		ret = 0;

		eem_debug("in eem debug proc write 3~~~~~~~~\n");
		memset(&eem_data, 0, sizeof(struct eem_ipi_data));
		eem_data.u.data.arg[0] = det_to_id(det);
		eem_data.u.data.arg[1] = enabled;
		ipi_ret = eem_to_cputoeb(IPI_EEMSN_DEBUG_PROC_WRITE, &eem_data);
		det->disabled = enabled;

	} else
		ret = -EINVAL;

out:
	eem_debug("in eem debug proc write 4~~~~~~~~\n");
	free_page((unsigned long)buf);
	FUNC_EXIT(FUNC_LV_HELP);

	return (ret < 0) ? ret : count;
}

/*
 * show current aging margin
 */
static int eem_setmargin_proc_show(struct seq_file *m, void *v)
{
	struct eemsn_det *det = (struct eemsn_det *)m->private;

	FUNC_ENTER(FUNC_LV_HELP);

	/* FIXME: EEMEN sometimes is disabled temp */
	seq_printf(m, "[%s] volt clamp:%d\n",
		   ((char *)(det->name) + 8),
		   det->volt_clamp);

	FUNC_EXIT(FUNC_LV_HELP);

	return 0;
}

/*
 * remove aging margin
 */
static ssize_t eem_setmargin_proc_write(struct file *file,
			const char __user *buffer, size_t count, loff_t *pos)
{
	int ret;
	int aging_val[2];
	int i = 0;
	int start_oft, end_oft;
	char *buf = (char *) __get_free_page(GFP_USER);
	struct eemsn_det *det = (struct eemsn_det *)PDE_DATA(file_inode(file));
	char *tok;
	char *cmd_str = NULL;

	FUNC_ENTER(FUNC_LV_HELP);

	if (!buf) {
		FUNC_EXIT(FUNC_LV_HELP);
		return -ENOMEM;
	}

	ret = -EINVAL;

	if (count >= PAGE_SIZE)
		goto out;

	ret = -EFAULT;

	if (copy_from_user(buf, buffer, count))
		goto out;

	buf[count] = '\0';

	cmd_str = strsep(&buf, " ");
	if (cmd_str == NULL)
		ret = -EINVAL;

	while ((tok = strsep(&buf, " ")) != NULL) {
		if (i == 3) {
			eem_error("number of arguments > 3!\n");
			goto out;
		}

		if (kstrtoint(tok, 10, &aging_val[i])) {
			eem_error("Invalid input: %s\n", tok);
			goto out;
		} else
			i++;
	}

	if (!strncmp(cmd_str, "aging", sizeof("aging"))) {
		start_oft = aging_val[0];
		end_oft = aging_val[1];
		//eem_calculate_aging_margin(det, start_oft, end_oft);

		ret = count;
	} else if (!strncmp(cmd_str, "clamp", sizeof("clamp"))) {
		if (aging_val[0] < 20)
			det->volt_clamp = aging_val[0];

		ret = count;
	} else {
		ret = -EINVAL;
		goto out;
	}

	//eem_set_eem_volt(det);

out:
	eem_debug("in eem debug proc write 4~~~~~~~~\n");
	free_page((unsigned long)buf);
	FUNC_EXIT(FUNC_LV_HELP);

	return ret;
}

/*
 * show current EEM data
 */
#if 0
void eem_dump_reg_by_det(struct eemsn_det *det, struct seq_file *m)
{
	unsigned int i, k;
#if DUMP_DATA_TO_DE
	unsigned int j;
#endif

	for (i = EEM_PHASE_INIT01; i <= EEM_PHASE_MON; i++) {
		seq_printf(m, "Bank_number = %d\n", det->det_id);
		if (i < EEM_PHASE_MON)
			seq_printf(m, "mode = init%d\n", i+1);
		else
			seq_puts(m, "mode = mon\n");
		if (eem_log_en) {
			seq_printf(m, "0x%08X, 0x%08X, 0x%08X, 0x%08X, 0x%08X\n",
				det->dcvalues[i],
				det->freqpct30[i],
				det->eem_26c[i],
				det->vop30[i],
				det->eem_eemEn[i]
			);

			if (det->eem_eemEn[i] == (0x5 | SEC_MOD_SEL)) {
				seq_printf(m, "EEM_LOG: Bank_number = [%d] (%d) - (",
				det->det_id, det->ops->get_temp(det));

				for (k = 0; k < NR_FREQ - 1; k++)
					seq_printf(m, "%d, ",
					det->ops->pmic_2_volt(det,
					det->volt_tbl_pmic[k]));
				seq_printf(m, "%d) - (",
						det->ops->pmic_2_volt(det,
						det->volt_tbl_pmic[k]));

				for (k = 0; k < NR_FREQ - 1; k++)
					seq_printf(m, "%d, ", det->freq_tbl[k]);
				seq_printf(m, "%d)\n", det->freq_tbl[k]);
			}
		} /* if (eem_log_en) */
#if DUMP_DATA_TO_DE
		for (j = 0; j < ARRAY_SIZE(reg_dump_addr_off); j++)
			seq_printf(m, "0x%08lx = 0x%08x\n",
			(unsigned long)EEM_BASEADDR + reg_dump_addr_off[j],
			det->reg_dump_data[j][i]
			);
#endif
	}
}
#endif

static void dump_sndata_to_de(struct seq_file *m)
{
	int *val = (int *)&eem_devinfo;
	int i, j, addr;

	seq_printf(m,
	"[%d]=================Start EEMSN dump===================\n",
	seq++);

	for (i = 0; i < sizeof(struct eemsn_devinfo) / sizeof(unsigned int);
		i++)
		seq_printf(m, "[%d]M_HW_RES%d\t= 0x%08X\n",
		seq++, ((i >= IDX_HW_RES_SN) ? (i + 3) : i), val[i]);

	seq_printf(m, "[%d]Start dump_CPE:\n", seq++);
	for (i = 0; i < MIN_SIZE_SN_DUMP_CPE; i++) {
		if (i == 5)
			addr = SN_CPEIRQSTS;
		else if (i == 6)
			addr = SN_CPEINTSTSRAW;
		else
			addr = (SN_COMPAREDVOP + i * 4);

		seq_printf(m, "[%d]0x%x = 0x%x\n",
			seq++, addr,
			eemsn_log->sn_log.reg_dump_cpe[i]);

	}
	seq_printf(m, "[%d]Start dump_sndata:\n", seq++);
	for (i = 0; i < SIZE_SN_DUMP_SENSOR; i++) {
		seq_printf(m, "[%d]0x%x = 0x%x\n",
			seq++, (SN_C0ASENSORDATA + i * 4),
			eemsn_log->sn_log.reg_dump_sndata[i]);
	}

	seq_printf(m, "[%d]start dump_sn_cpu:\n", seq++);
	for (i = 0; i < NUM_SN_CPU; i++) {
		for (j = 0; j < SIZE_SN_MCUSYS_REG; j++) {
			seq_printf(m, "[%d]0x%x = 0x%x\n",
				seq++, (sn_mcysys_reg_base[i] +
				sn_mcysys_reg_dump_off[j]),
				eemsn_log->sn_log.reg_dump_sn_cpu[i][j]);
		}
	}
	seq_printf(m,
	"[%d]=================End EEMSN dump===================\n",
	seq++);


}

static int eem_dump_proc_show(struct seq_file *m, void *v)
{
	struct eem_ipi_data eem_data;
	unsigned int ipi_ret = 0;
	unsigned int locklimit = 0;
	unsigned char lock;
	enum sn_det_id i;

	FUNC_ENTER(FUNC_LV_HELP);


	memset(&eem_data, 0, sizeof(struct eem_ipi_data));
	ipi_ret = eem_to_cputoeb(IPI_EEMSN_DUMP_PROC_SHOW, &eem_data);
	seq_printf(m, "ipi_ret:%d\n", ipi_ret);

	seq_printf(m, "[%d]========Start sn_trigger_sensing!\n", seq++);

	while (1) {
		lock = eemsn_log->lock;
		locklimit++;
		mdelay(5); /* wait 5 ms */
		/* eem_error("1 lock=0x%X\n", lock); */
		lock = eemsn_log->lock;
		/* eem_error("2 lock=0x%X\n", lock); */
		if ((lock & 0x1) && (locklimit < 5))
			continue; /* if lock, read dram again */
		else
			break;
		/* if unlock, break out while loop, read next det*/
	}

	for (i = 0; i < NR_SN_DET; i++) {

		if (i == SN_DET_B)
			seq_printf(m, "[%d]T_SVT_HV_BCPU:%d %d %d %d\n",
				seq++, eem_devinfo.T_SVT_HV_BCPU,
				eem_devinfo.T_SVT_LV_BCPU,
				eemsn_log->sn_cal_data[i].T_SVT_HV_RT,
				eemsn_log->sn_cal_data[i].T_SVT_LV_RT);
		else
			seq_printf(m, "[%d]T_SVT_HV_LCPU:%d %d %d %d\n",
				seq, eem_devinfo.T_SVT_HV_LCPU,
				eem_devinfo.T_SVT_LV_LCPU,
				eemsn_log->sn_cal_data[i].T_SVT_HV_RT,
				eemsn_log->sn_cal_data[i].T_SVT_LV_RT);

		seq_printf(m, "[%d]id:%d, ATE_Temp_decode:%d, T_SVT_current:%d, ",
			seq++, i, (eem_devinfo.ATE_TEMP * 10 + 35),
			eemsn_log->sn_log.sd[i].T_SVT_current);

		seq_printf(m, "Sensor_Volt_HT:%d, Sensor_Volt_RT:%d\n",
			eemsn_log->sn_log.sd[i].Sensor_Volt_HT,
			eemsn_log->sn_log.sd[i].Sensor_Volt_RT);

		seq_printf(m, "[%d]SN_Vmin:0x%x, CPE_Vmin:0x%x, init2[0]:0x%x, ",
			seq++, eemsn_log->sn_log.sd[i].SN_Vmin,
			eemsn_log->sn_log.sd[i].CPE_Vmin,
			eemsn_log->det_log[i].volt_tbl_init2[0]);
		seq_printf(m, "sn_aging:%d, SN_temp:%d, CPE_temp:%d\n",
			eemsn_log->sn_cal_data[i].sn_aging,
			eemsn_log->sn_log.sd[i].SN_temp,
			eemsn_log->sn_log.sd[i].CPE_temp);

		seq_printf(m, "cur_opp:%d, dst_volt_pmic:0x%x, footprint:0x%x\n",
			eemsn_log->sn_log.sd[i].cur_oppidx,
			eemsn_log->sn_log.sd[i].dst_volt_pmic,
			eemsn_log->sn_log.footprint[i]);
		seq_printf(m, "[%d]cur_volt:%d, new dst_volt_pmic:%d, cur temp:%d\n",
			seq++, eemsn_log->sn_log.sd[i].cur_volt,
			eemsn_log->sn_log.sd[i].dst_volt_pmic * CPU_PMIC_STEP,
			eemsn_log->sn_log.sd[i].cur_temp);
	}
	seq_printf(m, "allfp:0x%x\n",
		eemsn_log->sn_log.allfp);

	dump_sndata_to_de(m);

	FUNC_EXIT(FUNC_LV_HELP);
	return 0;
}

static int eem_aging_dump_proc_show(struct seq_file *m, void *v)
{
	struct eem_ipi_data eem_data;
	int ipi_ret = 0;
	unsigned char lock;
	unsigned int locklimit = 0;
	enum sn_det_id i;

	FUNC_ENTER(FUNC_LV_HELP);

	memset(&eem_data, 0, sizeof(struct eem_ipi_data));
	ipi_ret = eem_to_cputoeb(IPI_EEMSN_AGING_DUMP_PROC_SHOW, &eem_data);

	seq_printf(m, "efuse_sv:0x%x\n", eemsn_log->efuse_sv);

	seq_printf(m, "T_SVT_HV_LCPU:%d %d %d %d\n",
		eem_devinfo.T_SVT_HV_LCPU,
		eem_devinfo.T_SVT_LV_LCPU,
		eem_devinfo.T_SVT_HV_LCPU_RT,
		eem_devinfo.T_SVT_LV_LCPU_RT);

	seq_printf(m, "T_SVT_HV_BCPU:%d %d %d %d\n",
		eem_devinfo.T_SVT_HV_BCPU,
		eem_devinfo.T_SVT_LV_BCPU,
		eem_devinfo.T_SVT_HV_BCPU_RT,
		eem_devinfo.T_SVT_LV_BCPU_RT);

	seq_printf(m, "IN init_det, LCPU_A_T0_SVT:%d, LVT:%d, ",
		eem_devinfo.LCPU_A_T0_SVT,
		eem_devinfo.LCPU_A_T0_LVT);
	seq_printf(m, "ULVT:%d, DELTA_VC_LCPU:%d, ATE_TEMP:%d\n",
		eem_devinfo.LCPU_A_T0_ULVT,
		eem_devinfo.DELTA_VC_LCPU,
		eem_devinfo.ATE_TEMP);


	seq_printf(m, "IN init_det, BCPU_A_T0_SVT:%d, LVT:%d, ",
		eem_devinfo.BCPU_A_T0_SVT,
		eem_devinfo.BCPU_A_T0_LVT);
	seq_printf(m, "ULVT:%d, DELTA_VC_BCPU:%d\n",
		eem_devinfo.BCPU_A_T0_ULVT,
		eem_devinfo.DELTA_VC_BCPU);


	while (1) {
		lock = eemsn_log->lock;
		locklimit++;
		mdelay(5); /* wait 5 ms */
		/* eem_error("1 lock=0x%X\n", lock); */
		lock = eemsn_log->lock;
		/* eem_error("2 lock=0x%X\n", lock); */
		if ((lock & 0x1) && (locklimit < 5))
			continue; /* if lock, read dram again */
		else
			break;
		/* if unlock, break out while loop, read next det*/
	}

	for (i = 0; i < NR_SN_DET; i++) {
		seq_printf(m, "id:%d\n", i);
		seq_printf(m, "[cal_sn_aging]Param_temp:%d, SVT:%d, LVT:%d, ULVT:%d\n",
			eemsn_log->sn_cpu_param[i].Param_temp,
			eemsn_log->sn_cpu_param[i].Param_A_Tused_SVT,
			eemsn_log->sn_cpu_param[i].Param_A_Tused_LVT,
			eemsn_log->sn_cpu_param[i].Param_A_Tused_ULVT);
		seq_printf(m, "[INIT]delta_vc:%d, CPE_GB:%d, MSSV_GB:%d\n",
			eemsn_log->sn_cal_data[i].delta_vc,
			eemsn_log->sn_cpu_param[i].CPE_GB,
			eemsn_log->sn_cpu_param[i].MSSV_GB);
		seq_printf(m, "cal_sn_aging, atvt A_Tused_SVT:%d, LVT:%d, ",
			eemsn_log->sn_cal_data[i].atvt.A_Tused_SVT,
			eemsn_log->sn_cal_data[i].atvt.A_Tused_LVT);
		seq_printf(m, "ULVT:%d, cur temp:%d\n",
			eemsn_log->sn_cal_data[i].atvt.A_Tused_ULVT,
			eemsn_log->sn_cal_data[i].TEMP_CAL);

		seq_printf(m, "[cal_sn_aging]id:%d, cpe_init_aging:%llu, ",
			i, eemsn_log->sn_cal_data[i].cpe_init_aging);
		seq_printf(m, "delta_vc:%d, CPE_Aging:%d, sn_anging:%d\n",
			eemsn_log->sn_cal_data[i].delta_vc,
			eemsn_log->sn_cal_data[i].CPE_Aging,
			eemsn_log->sn_cal_data[i].sn_aging);
		seq_printf(m, "volt_cross:%d\n",
			eemsn_log->sn_cal_data[i].volt_cross);
	}

	if (ipi_ret != 0)
		seq_printf(m, "ipi_ret:%d\n", ipi_ret);

	FUNC_EXIT(FUNC_LV_HELP);
	return 0;
}

static int eem_sn_sram_proc_show(struct seq_file *m, void *v)
{
	phys_addr_t sn_mem_base_phys;
	phys_addr_t sn_mem_size;
	phys_addr_t sn_mem_base_virt = 0;
	void __iomem *addr_ptr;

	FUNC_ENTER(FUNC_LV_HELP);

	/* sn_mem_size = NR_FREQ * 2; */
	sn_mem_size = OFFS_SN_VOLT_E_4B - OFFS_SN_VOLT_S_4B;

	sn_mem_base_phys = OFFS_SN_VOLT_S_4B;
	if ((void __iomem *)sn_mem_base_phys != NULL)
		sn_mem_base_virt =
		(phys_addr_t)(uintptr_t)ioremap_wc(
		sn_mem_base_phys,
		sn_mem_size);
#if 0
	eem_error("phys:0x%llx, size:%d, virt:0x%llx\n",
		(unsigned long long)sn_mem_base_phys,
		(unsigned long long)sn_mem_size,
		(unsigned long long)sn_mem_base_virt);
	eem_error("read base_virt:0x%x\n",
		eem_read(sn_mem_base_virt));
#endif
	if ((void __iomem *)(sn_mem_base_virt) != NULL) {
		for (addr_ptr = (void __iomem *)(sn_mem_base_virt);
			addr_ptr <= ((void __iomem *)(sn_mem_base_virt) +
			OFFS_SN_VOLT_E_4B - OFFS_SN_VOLT_S_4B);
			(addr_ptr += 4))
			seq_printf(m, "0x%08X\n",
				(unsigned int)eem_read(addr_ptr));
	}
	FUNC_EXIT(FUNC_LV_HELP);
	return 0;
}

static int eem_hrid_proc_show(struct seq_file *m, void *v)
{
	unsigned int i;

	FUNC_ENTER(FUNC_LV_HELP);
	for (i = 0; i < 4; i++)
		seq_printf(m, "%s[HRID][%d]: 0x%08X\n", EEM_TAG, i,
			get_devinfo_with_index(DEVINFO_HRID_0 + i));

	FUNC_EXIT(FUNC_LV_HELP);
	return 0;
}

static int eem_efuse_proc_show(struct seq_file *m, void *v)
{
	int *val = (int *)&eem_devinfo;
	unsigned int i;

	FUNC_ENTER(FUNC_LV_HELP);
	for (i = 0; i < 25; i++)
		seq_printf(m, "%s[PTP_DUMP] ORIG_RES%d: 0x%08X\n", EEM_TAG, i,
			get_devinfo_with_index(DEVINFO_IDX_0 + i));
	seq_printf(m, "%s[PTP_DUMP] ORIG_RES%d: 0x%08X\n", EEM_TAG, i,
		get_devinfo_with_index(DEVINFO_IDX_25));


	/* Depend on EFUSE location */
	for (i = 0; i < IDX_HW_RES_SN;
		i++)
		seq_printf(m, "%s[PTP_DUMP] RES%d: 0x%08X\n", EEM_TAG, i,
			val[i]);

	for (i = IDX_HW_RES_SN; i < NR_HW_RES_FOR_BANK; i++)
		seq_printf(m, "%s[PTP_DUMP] RES%d: 0x%08X\n", EEM_TAG,
			(i + 3),
			val[i]);

	FUNC_EXIT(FUNC_LV_HELP);
	return 0;
}

static int eem_freq_proc_show(struct seq_file *m, void *v)
{
	struct eemsn_det *det;
	unsigned int i;
	enum mt_cpu_dvfs_id cpudvfsindex;

	FUNC_ENTER(FUNC_LV_HELP);
	for_each_det(det) {
		cpudvfsindex = detid_to_dvfsid(det);
		for (i = 0; i < NR_FREQ_CPU; i++) {
			if (det->det_id <= EEMSN_DET_CCI) {
				seq_printf(m,
					"%s[DVFS][CPU_%s][OPP%d] volt:%d, freq:%d\n",
					EEM_TAG, cpu_name[cpudvfsindex], i,
					det->ops->pmic_2_volt(det,
					det->volt_tbl_orig[i]) * 10,
#if SET_PMIC_VOLT_TO_DVFS
					mt_cpufreq_get_freq_by_idx(cpudvfsindex,
									i)
					/ 1000
#else
					0
#endif
					);
			}
		}
	}

	FUNC_EXIT(FUNC_LV_HELP);
	return 0;
}

static int eem_mar_proc_show(struct seq_file *m, void *v)
{
	FUNC_ENTER(FUNC_LV_HELP);

	seq_printf(m, "%s[CPU_BIG][HIGH] 1:%d, 2:%d, 3:%d, 5:%d\n",
			EEM_TAG, LOW_TEMP_OFF, 0,
			HIGH_TEMP_OFF, AGING_VAL_CPU_B);

	seq_printf(m, "%s[CPU_BIG][MID] 1:%d, 2:%d, 3:%d, 5:%d\n",
			EEM_TAG, LOW_TEMP_OFF, 0,
			HIGH_TEMP_OFF, AGING_VAL_CPU_B);

	seq_printf(m, "%s[CPU_L][HIGH] 1:%d, 2:%d, 3:%d, 5:%d\n",
			EEM_TAG, LOW_TEMP_OFF, 0,
			HIGH_TEMP_OFF, AGING_VAL_CPU);

	seq_printf(m, "%s[CPU_CCI][HIGH] 1:%d, 2:%d, 3:%d, 5:%d\n",
			EEM_TAG, LOW_TEMP_OFF, 0,
			HIGH_TEMP_OFF, AGING_VAL_CPU);

	FUNC_EXIT(FUNC_LV_HELP);
	return 0;
}


/*
 * show current voltage
 */
static int eem_cur_volt_proc_show(struct seq_file *m, void *v)
{
	struct eemsn_det *det = (struct eemsn_det *)m->private;
	u32 rdata = 0, i;

	FUNC_ENTER(FUNC_LV_HELP);

	rdata = det->ops->get_volt(det);

	if (rdata != 0)
		seq_printf(m, "%d\n", rdata);
	else
		seq_printf(m, "EEM[%s] read current voltage fail\n", det->name);

	if (det->features != 0) {
		for (i = 0; i < NR_FREQ; i++)
			seq_printf(m, "[%d],eem = [%x], pmic = [%x], volt = [%d]\n",
			i,
			eemsn_log->det_log[det->det_id].volt_tbl_init2[i],
			eemsn_log->det_log[det->det_id].volt_tbl_pmic[i],
			det->ops->pmic_2_volt(det,
			eemsn_log->det_log[det->det_id].volt_tbl_pmic[i]));
	}
	FUNC_EXIT(FUNC_LV_HELP);

	return 0;
}

/*
 * show current EEM status
 */
static int eem_status_proc_show(struct seq_file *m, void *v)
{
	int i;
	struct eemsn_det *det = (struct eemsn_det *)m->private;

	FUNC_ENTER(FUNC_LV_HELP);

	seq_printf(m, "bank = %d, feature:0x%x, T(%d) - (",
		   det->det_id, det->features, det->ops->get_temp(det));
	for (i = 0; i < NR_FREQ - 1; i++)
		seq_printf(m, "%d, ", det->ops->pmic_2_volt(det,
					det->volt_tbl_pmic[i]));
	seq_printf(m, "%d) - (",
			det->ops->pmic_2_volt(det, det->volt_tbl_pmic[i]));

	for (i = 0; i < NR_FREQ - 1; i++)
		seq_printf(m, "%d, ", det->freq_tbl[i]);
	seq_printf(m, "%d)\n", det->freq_tbl[i]);

	FUNC_EXIT(FUNC_LV_HELP);

	return 0;
}
/*
 * set EEM log enable by procfs interface
 */

static int eem_log_en_proc_show(struct seq_file *m, void *v)
{
	struct eem_ipi_data eem_data;
	unsigned int ipi_ret = 0;

	FUNC_ENTER(FUNC_LV_HELP);
	memset(&eem_data, 0, sizeof(struct eem_ipi_data));
	ipi_ret = eem_to_cputoeb(IPI_EEMSN_LOGEN_PROC_SHOW, &eem_data);
	seq_printf(m, "kernel:%d, EB:%d\n", eem_log_en, ipi_ret);
	FUNC_EXIT(FUNC_LV_HELP);


	return 0;
}

static ssize_t eem_log_en_proc_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *pos)
{
	int ret;
	char *buf = (char *) __get_free_page(GFP_USER);
	struct eem_ipi_data eem_data;
	unsigned int ipi_ret = 0;

	FUNC_ENTER(FUNC_LV_HELP);

	if (!buf) {
		FUNC_EXIT(FUNC_LV_HELP);
		return -ENOMEM;
	}

	ret = -EINVAL;

	if (count >= PAGE_SIZE)
		goto out;

	ret = -EFAULT;

	if (copy_from_user(buf, buffer, count))
		goto out;

	buf[count] = '\0';

	ret = -EINVAL;

	if (kstrtoint(buf, 10, &eem_log_en)) {
		eem_debug("bad argument!! Should be \"0\" or \"1\"\n");
		goto out;
	}

	ret = 0;
	memset(&eem_data, 0, sizeof(struct eem_ipi_data));
	eem_data.u.data.arg[0] = eem_log_en;
	ipi_ret = eem_to_cputoeb(IPI_EEMSN_LOGEN_PROC_WRITE, &eem_data);


out:
	free_page((unsigned long)buf);
	FUNC_EXIT(FUNC_LV_HELP);

	return (ret < 0) ? ret : count;
}

static int eem_en_proc_show(struct seq_file *m, void *v)
{
	struct eem_ipi_data eem_data;
	unsigned int ipi_ret = 0;

	FUNC_ENTER(FUNC_LV_HELP);
	memset(&eem_data, 0, sizeof(struct eem_ipi_data));
	ipi_ret = eem_to_cputoeb(IPI_EEMSN_EN_PROC_SHOW, &eem_data);
	seq_printf(m, "kernel:%d, EB:%d\n", ctrl_EEMSN_Enable, ipi_ret);
	FUNC_EXIT(FUNC_LV_HELP);


	return 0;
}

static ssize_t eem_en_proc_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *pos)
{
	int ret;
	char *buf = (char *) __get_free_page(GFP_USER);
	struct eem_ipi_data eem_data;
	unsigned int ipi_ret = 0;

	FUNC_ENTER(FUNC_LV_HELP);

	if (!buf) {
		FUNC_EXIT(FUNC_LV_HELP);
		return -ENOMEM;
	}

	ret = -EINVAL;

	if (count >= PAGE_SIZE)
		goto out;

	ret = -EFAULT;

	if (copy_from_user(buf, buffer, count))
		goto out;

	buf[count] = '\0';

	ret = -EINVAL;

	if (kstrtoint(buf, 10, &ctrl_EEMSN_Enable)) {
		eem_debug("bad argument!! Should be \"0\" or \"1\"\n");
		goto out;
	}

	ret = 0;
	memset(&eem_data, 0, sizeof(struct eem_ipi_data));
	eemsn_log->eemsn_enable = ctrl_EEMSN_Enable;
	eem_data.u.data.arg[0] = ctrl_EEMSN_Enable;
	ipi_ret = eem_to_cputoeb(IPI_EEMSN_EN_PROC_WRITE, &eem_data);


out:
	free_page((unsigned long)buf);
	FUNC_EXIT(FUNC_LV_HELP);

	return (ret < 0) ? ret : count;
}

static int eem_sn_en_proc_show(struct seq_file *m, void *v)
{
	struct eem_ipi_data eem_data;
	unsigned int ipi_ret = 0;

	FUNC_ENTER(FUNC_LV_HELP);
	memset(&eem_data, 0, sizeof(struct eem_ipi_data));
	ipi_ret = eem_to_cputoeb(IPI_EEMSN_SNEN_PROC_SHOW, &eem_data);
	seq_printf(m, "kernel:%d, EB:%d\n", ctrl_SN_Enable, ipi_ret);
	FUNC_EXIT(FUNC_LV_HELP);


	return 0;
}

static ssize_t eem_sn_en_proc_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *pos)
{
	int ret;
	char *buf = (char *) __get_free_page(GFP_USER);
	struct eem_ipi_data eem_data;
	unsigned int ipi_ret = 0;

	FUNC_ENTER(FUNC_LV_HELP);

	if (!buf) {
		FUNC_EXIT(FUNC_LV_HELP);
		return -ENOMEM;
	}

	ret = -EINVAL;

	if (count >= PAGE_SIZE)
		goto out;

	ret = -EFAULT;

	if (copy_from_user(buf, buffer, count))
		goto out;

	buf[count] = '\0';

	ret = -EINVAL;

	if (kstrtoint(buf, 10, &ctrl_SN_Enable)) {
		eem_debug("bad argument!! Should be \"0\" or \"1\"\n");
		goto out;
	}


	ret = 0;
	memset(&eem_data, 0, sizeof(struct eem_ipi_data));
	eemsn_log->sn_enable = ctrl_SN_Enable;
	eem_data.u.data.arg[0] = ctrl_SN_Enable;
	ipi_ret = eem_to_cputoeb(IPI_EEMSN_SNEN_PROC_WRITE, &eem_data);


out:
	free_page((unsigned long)buf);
	FUNC_EXIT(FUNC_LV_HELP);

	return (ret < 0) ? ret : count;
}

static int eem_force_sensing_proc_show(struct seq_file *m, void *v)
{
	struct eem_ipi_data eem_data;
	unsigned int ipi_ret = 0;

	FUNC_ENTER(FUNC_LV_HELP);
	memset(&eem_data, 0, sizeof(struct eem_ipi_data));
	ipi_ret = eem_to_cputoeb(IPI_EEMSN_FORCE_SN_SENSING, &eem_data);
	seq_printf(m, "ret:%d\n", ipi_ret);
	FUNC_EXIT(FUNC_LV_HELP);

	return 0;
}

static int eem_pull_data_proc_show(struct seq_file *m, void *v)
{
	struct eem_ipi_data eem_data;
	unsigned int ipi_ret = 0;

	FUNC_ENTER(FUNC_LV_HELP);
	memset(&eem_data, 0, sizeof(struct eem_ipi_data));
	ipi_ret = eem_to_cputoeb(IPI_EEMSN_PULL_DATA, &eem_data);
	seq_printf(m, "ret:%d\n", ipi_ret);
	FUNC_EXIT(FUNC_LV_HELP);

	return 0;
}

/*
 * show EEM offset
 */
static int eem_offset_proc_show(struct seq_file *m, void *v)
{
	struct eemsn_det *det = (struct eemsn_det *)m->private;

	FUNC_ENTER(FUNC_LV_HELP);

	seq_printf(m, "%d\n", det->volt_offset);

	FUNC_EXIT(FUNC_LV_HELP);

	return 0;
}

/*
 * set EEM offset by procfs
 */
static ssize_t eem_offset_proc_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *pos)
{
	int ret;
	char *buf = (char *) __get_free_page(GFP_USER);
	int offset = 0;
	struct eemsn_det *det = (struct eemsn_det *)PDE_DATA(file_inode(file));
	unsigned int ipi_ret = 0;
	struct eem_ipi_data eem_data;


	FUNC_ENTER(FUNC_LV_HELP);

	if (!buf) {
		FUNC_EXIT(FUNC_LV_HELP);
		return -ENOMEM;
	}

	ret = -EINVAL;

	if (count >= PAGE_SIZE)
		goto out;

	ret = -EFAULT;

	if (copy_from_user(buf, buffer, count))
		goto out;

	buf[count] = '\0';

	if (!kstrtoint(buf, 10, &offset)) {
		ret = 0;
		memset(&eem_data, 0, sizeof(struct eem_ipi_data));
		eem_data.u.data.arg[0] = det_to_id(det);
		eem_data.u.data.arg[1] = offset;
		ipi_ret = eem_to_cputoeb(IPI_EEMSN_OFFSET_PROC_WRITE, &eem_data);
		/* to show in eem_offset_proc_show */
		det->volt_offset = (signed char)offset;
		eem_debug("set volt_offset %d(%d)\n", offset, det->volt_offset);
	} else {
		ret = -EINVAL;
		eem_debug("bad argument_1!! argument should be \"0\"\n");
	}

out:
	free_page((unsigned long)buf);
	FUNC_EXIT(FUNC_LV_HELP);

	return (ret < 0) ? ret : count;
}

#define PROC_FOPS_RW(name)					\
	static int name ## _proc_open(struct inode *inode,	\
		struct file *file)				\
	{							\
		return single_open(file, name ## _proc_show,	\
			PDE_DATA(inode));			\
	}							\
	static const struct file_operations name ## _proc_fops = {	\
		.owner		= THIS_MODULE,				\
		.open		= name ## _proc_open,			\
		.read		= seq_read,				\
		.llseek		= seq_lseek,				\
		.release	= single_release,			\
		.write		= name ## _proc_write,			\
	}

#define PROC_FOPS_RO(name)					\
	static int name ## _proc_open(struct inode *inode,	\
		struct file *file)				\
	{							\
		return single_open(file, name ## _proc_show,	\
			PDE_DATA(inode));			\
	}							\
	static const struct file_operations name ## _proc_fops = {	\
		.owner		= THIS_MODULE,				\
		.open		= name ## _proc_open,			\
		.read		= seq_read,				\
		.llseek		= seq_lseek,				\
		.release	= single_release,			\
	}

#define PROC_ENTRY(name)	{__stringify(name), &name ## _proc_fops}

PROC_FOPS_RW(eem_debug);
PROC_FOPS_RO(eem_status);
PROC_FOPS_RO(eem_cur_volt);
PROC_FOPS_RW(eem_offset);
PROC_FOPS_RO(eem_dump);
PROC_FOPS_RO(eem_aging_dump);
PROC_FOPS_RO(eem_sn_sram);
PROC_FOPS_RO(eem_hrid);
PROC_FOPS_RO(eem_efuse);
PROC_FOPS_RO(eem_freq);
PROC_FOPS_RO(eem_mar);
PROC_FOPS_RW(eem_log_en);
PROC_FOPS_RW(eem_en);
PROC_FOPS_RW(eem_sn_en);
PROC_FOPS_RO(eem_force_sensing);
PROC_FOPS_RO(eem_pull_data);
PROC_FOPS_RW(eem_setmargin);
#if ENABLE_INIT1_STRESS
PROC_FOPS_RW(eem_init1stress_en);
#endif

static int create_procfs(void)
{
	struct proc_dir_entry *eem_dir = NULL;
	struct proc_dir_entry *det_dir = NULL;
	int i;
	struct eemsn_det *det;

	struct pentry {
		const char *name;
		const struct file_operations *fops;
	};

	struct pentry det_entries[] = {
		PROC_ENTRY(eem_debug),
		PROC_ENTRY(eem_status),
		PROC_ENTRY(eem_cur_volt),
		PROC_ENTRY(eem_offset),
		PROC_ENTRY(eem_setmargin),
	};

	struct pentry eem_entries[] = {
		PROC_ENTRY(eem_dump),
		PROC_ENTRY(eem_aging_dump),
		PROC_ENTRY(eem_sn_sram),
		PROC_ENTRY(eem_hrid),
		PROC_ENTRY(eem_efuse),
		PROC_ENTRY(eem_freq),
		PROC_ENTRY(eem_mar),
		PROC_ENTRY(eem_log_en),
		PROC_ENTRY(eem_en),
		PROC_ENTRY(eem_sn_en),
		PROC_ENTRY(eem_force_sensing),
		PROC_ENTRY(eem_pull_data),

#if ENABLE_INIT1_STRESS
		PROC_ENTRY(eem_init1stress_en),
#endif
	};

	FUNC_ENTER(FUNC_LV_HELP);

	/* create procfs root /proc/eem */
	eem_dir = proc_mkdir("eem", NULL);

	if (!eem_dir) {
		eem_error("[%s]: mkdir /proc/eem failed\n", __func__);
		FUNC_EXIT(FUNC_LV_HELP);
		return -1;
	}

	/* if ctrl_EEMSN_Enable =1, and has efuse value,
	 * create other banks procfs
	 */
	if (ctrl_EEMSN_Enable != 0 && eem_checkEfuse == 1) {
		for (i = 0; i < ARRAY_SIZE(eem_entries); i++) {
			if (!proc_create(eem_entries[i].name, 0664,
						eem_dir, eem_entries[i].fops)) {
				eem_error("[%s]: create /proc/eem/%s failed\n",
						__func__,
						eem_entries[i].name);
				FUNC_EXIT(FUNC_LV_HELP);
				return -3;
			}
		}

		for_each_det(det) {
			if (det->features == 0)
				continue;

			det_dir = proc_mkdir(det->name, eem_dir);

			if (!det_dir) {
				eem_debug("[%s]: mkdir /proc/eem/%s failed\n"
						, __func__, det->name);
				FUNC_EXIT(FUNC_LV_HELP);
				return -2;
			}

			for (i = 0; i < ARRAY_SIZE(det_entries); i++) {
				if (!proc_create_data(det_entries[i].name,
					0664,
					det_dir,
					det_entries[i].fops, det)) {
					eem_debug
			("[%s]: create /proc/eem/%s/%s failed\n", __func__,
			det->name, det_entries[i].name);
				FUNC_EXIT(FUNC_LV_HELP);
				return -3;
				}
			}
		}

	} /* if (ctrl_EEMSN_Enable != 0) */

	FUNC_EXIT(FUNC_LV_HELP);
	return 0;
}
#endif /* CONFIG_PROC_FS */


unsigned int get_efuse_status(void)
{
	return eem_checkEfuse;
}


/*
 * Module driver
 */
static int __init eem_init(void)
{
#ifdef CONFIG_MTK_TINYSYS_MCUPM_SUPPORT
	struct eem_ipi_data eem_data;
struct eemsn_det *det;
#endif
	int err = 0;
#if defined(MC50_LOAD)
	void __iomem *spare1_phys;
#endif

	eem_debug("[EEM] ctrl_EEMSN_Enable=%d\n", ctrl_EEMSN_Enable);
	get_devinfo();

#ifdef CONFIG_MTK_TINYSYS_MCUPM_SUPPORT

	err = mtk_ipi_register(&mcupm_ipidev, CH_S_EEMSN, NULL, NULL,
		(void *)&ipi_ackdata);
	if (err != 0) {
		eem_error("%s error ret:%d\n", __func__, err);
		return 0;
	}

	eem_log_phy_addr =
		mcupm_reserve_mem_get_phys(MCUPM_EEMSN_MEM_ID);
	eem_log_virt_addr =
		mcupm_reserve_mem_get_virt(MCUPM_EEMSN_MEM_ID);
	eem_log_size = sizeof(struct eemsn_log);
	eemsn_log = (struct eemsn_log *)eem_log_virt_addr;

	memset(eemsn_log, 0, sizeof(struct eemsn_log));
	memset(&eem_data, 0, sizeof(struct eem_ipi_data));
	eem_data.u.data.arg[0] = eem_log_phy_addr;
	eem_data.u.data.arg[1] = eem_log_size;
	eem_debug("arg0(addr):0x%x, arg1(len):%d",
		eem_data.u.data.arg[0], eem_data.u.data.arg[1]);
	memcpy(&(eemsn_log->efuse_devinfo), &eem_devinfo,
		sizeof(struct eemsn_devinfo));
	eemsn_log->segCode = get_devinfo_with_index(DEVINFO_SEG_IDX)
			& 0xFF;
	/* eemsn_log->efuse_sv = eem_read(EEM_TEMPSPARE1); */

#if defined(MC50_LOAD)
	/* for MC50 */
	spare1_phys = ioremap(EEM_PHY_TEMPSPARE1, 0);
	if ((void __iomem *)spare1_phys != NULL)
		eem_write(spare1_phys, SPARE1_VAL);
	else
		eem_error("incorrect spare1_phys:0x%x", spare1_phys);
#endif
	/* get original volt from cpu dvfs before init01 */
	for_each_det(det) {

		get_freq_table_cpu(det);
		memcpy(eemsn_log->det_log[det->det_id].freq_tbl,
			det->freq_tbl, sizeof(det->freq_tbl));
		eemsn_log->det_log[det->det_id].turn_pt =
			det->turn_pt;
		eemsn_log->det_log[det->det_id].num_freq_tbl =
			det->num_freq_tbl;

		get_orig_volt_table_cpu(det);
		memcpy(eemsn_log->det_log[det->det_id].volt_tbl_orig,
			det->volt_tbl_orig, sizeof(det->volt_tbl_orig));
	}

#if defined(AGING_LOAD)
	eem_error("@%s: AGING flavor name: %s\n",
		__func__, CONFIG_BUILD_ARM64_DTB_OVERLAY_IMAGE_NAMES);
	ctrl_agingload_enable = 1;
	eemsn_log->ctrl_aging_Enable = ctrl_agingload_enable;
#endif

	eem_to_cputoeb(IPI_EEMSN_SHARERAM_INIT, &eem_data);
#else
	return 0;
#endif
	eem_debug("AP:eem_log_size:%d, eemsn_log:%d\n",
		eem_data.u.data.arg[1], sizeof(struct eemsn_log));
	eem_debug("AP:%d, %d, %d, %d, %d\n",
	sizeof(struct eemsn_log_det),
	sizeof(struct sn_log_data),
	sizeof(struct sn_log_cal_data),
	sizeof(struct sn_param),
	sizeof(struct eemsn_devinfo));

	if (eem_checkEfuse == 0) {
		eem_error("eem_checkEfuse = 0\n");
		FUNC_EXIT(FUNC_LV_MODULE);
		return 0;
	}

	/*
	 * reg platform device driver
	 */
	err = platform_driver_register(&eem_driver);

	if (err) {
		eem_debug("EEM driver callback register failed..\n");
		FUNC_EXIT(FUNC_LV_MODULE);
		return err;
	}

	return 0;
}

static void __exit eem_exit(void)
{
	FUNC_ENTER(FUNC_LV_MODULE);
	eem_debug("eem de-initialization\n");
	FUNC_EXIT(FUNC_LV_MODULE);
}

late_initcall(eem_init); /* late_initcall */
#endif /* EN_EEM */

MODULE_DESCRIPTION("MediaTek EEM Driver v0.3");
MODULE_LICENSE("GPL");

#undef __MTK_EEM_C__
