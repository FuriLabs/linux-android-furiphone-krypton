/*
 *
 * FocalTech fts TouchScreen driver.
 *
 * Copyright (c) 2012-2021, Focaltech Ltd. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/*****************************************************************************
*
* File Name: focaltech_upgrade_ft8006s_an.c
*
* Author: Focaltech Driver Team
*
* Created: 2021-06-09
*
* Abstract:
*
* Reference:
*
*****************************************************************************/

/*****************************************************************************
* 1.Included header files
*****************************************************************************/
#include "../focaltech_flash.h"

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/
u8 pb_file_ft8006s_an[] = {
#include "../include/pramboot/FT8006S_AN_Pramboot_V1.3_20210617.i"
};

/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/

/*****************************************************************************
* Static function prototypes
*****************************************************************************/
#define LIC_FS_H_OFF                0
#define LIC_FS_L_OFF                1
#define LIC_CHECKSUM_H_OFF          2
#define LIC_CHECKSUM_L_OFF          3
#define LIC_LCD_LEN_H_OFF           4
#define LIC_LCD_LEN_L_OFF           5
#define LIC_ECC_H_OFF               6
#define LIC_ECC_L_OFF               7
#define LIC_ECC_START_OFF           12

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/
/* calculate lcd init code checksum */
static u16 fts_ft8006s_an_cal_lic_checksum(u8 *ptr , int length)
{
    /* CRC16 */
    u16 cfcs = 0;
    int i = 0;
    int j = 0;

    length = (length % 2 == 0) ? length : (length - 1);

    for (i = 0; i < length; i += 2) {
        cfcs ^= ((ptr[i] << 8) + ptr[i + 1]);
        for (j = 0; j < 16; j++) {
            if (cfcs & 1) {
                cfcs = (u16)((cfcs >> 1) ^ ((1 << 15) + (1 << 10) + (1 << 3)));
            } else {
                cfcs >>= 1;
            }
        }
    }
    return cfcs;
}

/*
 * fts_ft8006s_an_check_lic_valid - check initial code valid or not
 */
static int fts_ft8006s_an_check_lic_valid(u8 *buf)
{
    u16 initcode_checksum = 0;
    u16 buf_checksum = 0;
    u16 hlic_len = 0;

    hlic_len = (u16)(((u16)buf[2]) << 8) + buf[3];
    if ((hlic_len >= FTS_MAX_LEN_SECTOR) || (hlic_len <= FTS_MIN_LEN)) {
        FTS_ERROR("host lcd init code len(%x) is too large", hlic_len);
        return -EINVAL;
    }

    initcode_checksum = fts_ft8006s_an_cal_lic_checksum(buf + 2, hlic_len - 2);
    buf_checksum = ((u16)((u16)buf[0] << 8) + buf[1]);
    FTS_INFO("lcd init code calc checksum:0x%04x,0x%04x", initcode_checksum, buf_checksum);
    if (initcode_checksum != buf_checksum) {
        FTS_ERROR("Initial Code checksum fail");
        return -EINVAL;
    }

    return 0;
}

/*
 * fts_ft8006s_an_get_hlic_ver - read host lcd init code version
 *
 * return 0 if host lcd init code is valid, otherwise return error code
 */
static int fts_ft8006s_an_get_hlic_ver(u8 *initcode)
{
    u8 *hlic_buf = initcode;
    u16 hlic_len = 0;
    u8 hlic_ver[2] = { 0 };

    hlic_len = (u16)(((u16)hlic_buf[2]) << 8) + hlic_buf[3];
    FTS_INFO("host lcd init code len:%x", hlic_len);
    if ((hlic_len >= FTS_MAX_LEN_SECTOR) || (hlic_len <= FTS_MIN_LEN)) {
        FTS_ERROR("host lcd init code len(%x) is too large", hlic_len);
        return -EINVAL;
    }

    hlic_ver[0] = hlic_buf[hlic_len];
    hlic_ver[1] = hlic_buf[hlic_len + 1];

    FTS_INFO("host lcd init code ver:%x %x", hlic_ver[0], hlic_ver[1]);
    if (0xFF != (hlic_ver[0] + hlic_ver[1])) {
        FTS_ERROR("host lcd init code version check fail");
        return -EINVAL;
    }

    return hlic_ver[0];
}


static int fts_ft8006s_an_upgrade_mode(enum FW_FLASH_MODE mode, u8 *buf, u32 len)
{
    int ret = 0;
    u32 start_addr = 0;
    u8 cmd[4] = { 0 };
    u32 delay = 0;
    int ecc_in_host = 0;
    int ecc_in_tp = 0;

    if ((NULL == buf) || (len < FTS_MIN_LEN)) {
        FTS_ERROR("buffer/len(%x) is invalid", len);
        return -EINVAL;
    }

    /* enter into upgrade environment */
    ret = fts_fwupg_enter_into_boot();
    if (ret < 0) {
        FTS_ERROR("enter into pramboot/bootloader fail,ret=%d", ret);
        goto fw_reset;
    }

    cmd[0] = FTS_CMD_FLASH_MODE;
    cmd[1] = FLASH_MODE_UPGRADE_VALUE;
    if (upgrade_func_ft8006s_an.appoff_handle_in_ic) {
        start_addr = 0; /* offset handle in pramboot */
    } else {
        start_addr = upgrade_func_ft8006s_an.appoff;
    }
    if (FLASH_MODE_LIC == mode) {
        /* lcd initial code upgrade */
        cmd[1] = FLASH_MODE_LIC_VALUE;
    } else if (FLASH_MODE_PARAM == mode) {
        cmd[1] = FLASH_MODE_PARAM_VALUE;
    }
    FTS_INFO("flash mode:0x%02x, start addr=0x%04x", cmd[1], start_addr);

    ret = fts_write(cmd, 2);
    if (ret < 0) {
        FTS_ERROR("upgrade mode(09) cmd write fail");
        goto fw_reset;
    }

    cmd[0] = FTS_CMD_APP_DATA_LEN_INCELL;
    cmd[1] = BYTE_OFF_16(len);
    cmd[2] = BYTE_OFF_8(len);
    cmd[3] = BYTE_OFF_0(len);
    ret = fts_write(cmd, FTS_CMD_DATA_LEN_LEN);
    if (ret < 0) {
        FTS_ERROR("data len cmd write fail");
        goto fw_reset;
    }

    delay = FTS_ERASE_SECTOR_DELAY * (len / FTS_MAX_LEN_SECTOR);
    ret = fts_fwupg_erase(delay);
    if (ret < 0) {
        FTS_ERROR("erase cmd write fail");
        goto fw_reset;
    }

    /* write app */
    ecc_in_host = fts_flash_write_buf(start_addr, buf, len, 1);
    if (ecc_in_host < 0 ) {
        FTS_ERROR("flash write fail");
        goto fw_reset;
    }

    /* ecc */
    ecc_in_tp = fts_fwupg_ecc_cal(start_addr, len);
    if (ecc_in_tp < 0 ) {
        FTS_ERROR("ecc read fail");
        goto fw_reset;
    }

    FTS_INFO("ecc in tp:%x, host:%x", ecc_in_tp, ecc_in_host);
    if (ecc_in_tp != ecc_in_host) {
        FTS_ERROR("ecc check fail");
        goto fw_reset;
    }

    FTS_INFO("upgrade success, reset to normal boot");
    ret = fts_fwupg_reset_in_boot();
    if (ret < 0) {
        FTS_ERROR("reset to normal boot fail");
    }

    msleep(400);
    return 0;

fw_reset:
    return -EIO;
}

/************************************************************************
* Name: fts_ft8006s_an_upgrade
* Brief:
* Input:
* Output:
* Return: return 0 if success, otherwise return error code
***********************************************************************/
static int fts_ft8006s_an_upgrade(u8 *buf, u32 len)
{
    int ret = 0;
    u8 *tmpbuf = NULL;
    u32 app_len = 0;

    FTS_INFO("fw app upgrade...");
    if (NULL == buf) {
        FTS_ERROR("fw buf is null");
        return -EINVAL;
    }

    if ((len < FTS_MIN_LEN) || (len > FTS_MAX_LEN_FILE)) {
        FTS_ERROR("fw buffer len(%x) fail", len);
        return -EINVAL;
    }

    app_len = len - upgrade_func_ft8006s_an.appoff;
    tmpbuf = buf + upgrade_func_ft8006s_an.appoff;
    ret = fts_ft8006s_an_upgrade_mode(FLASH_MODE_APP, tmpbuf, app_len);
    if (ret < 0) {
        FTS_INFO("fw upgrade fail,reset to normal boot");
        if (fts_fwupg_reset_in_boot() < 0) {
            FTS_ERROR("reset to normal boot fail");
        }
        return ret;
    }

    return 0;
}

static int fts_ft8006s_an_lic_upgrade(u8 *buf, u32 len)
{
    int ret = 0;
    u8 *tmpbuf = NULL;
    u32 lic_len = 0;

    FTS_INFO("lcd initial code upgrade...");
    if (NULL == buf) {
        FTS_ERROR("lcd initial code buffer is null");
        return -EINVAL;
    }

    if ((len < FTS_MIN_LEN) || (len > FTS_MAX_LEN_FILE)) {
        FTS_ERROR("lcd initial code buffer len(%x) fail", len);
        return -EINVAL;
    }

    ret = fts_ft8006s_an_check_lic_valid(buf);
    if (ret < 0) {
        FTS_ERROR("initial code invalid, not upgrade lcd init code");
        return -EINVAL;
    }

    /* remalloc memory for initcode, need change content of initcode afterwise */
    lic_len = FTS_MAX_LEN_SECTOR;
    tmpbuf = kzalloc(lic_len, GFP_KERNEL);
    if (NULL == tmpbuf) {
        FTS_ERROR("initial code buf malloc fail");
        return -EINVAL;
    }
    memcpy(tmpbuf, buf, lic_len);

    ret = fts_ft8006s_an_upgrade_mode(FLASH_MODE_LIC, tmpbuf, lic_len);
    if (ret < 0) {
        FTS_INFO("lcd initial code upgrade fail,reset to normal boot");
        if (fts_fwupg_reset_in_boot() < 0) {
            FTS_ERROR("reset to normal boot fail");
        }
        if (tmpbuf) {
            kfree(tmpbuf);
            tmpbuf = NULL;
        }
        return ret;
    }

    if (tmpbuf) {
        kfree(tmpbuf);
        tmpbuf = NULL;
    }
    return 0;
}

struct upgrade_func upgrade_func_ft8006s_an = {
    .ctype = {0x22},
    .fwveroff = 0x110E,
    .fwcfgoff = 0xF80,
    .appoff = 0x1000,
    .licoff = 0x0000,
    .appoff_handle_in_ic = true,
    .upgspec_version = UPGRADE_SPEC_V_1_0,
    .pramboot_supported = true,
    .pramboot = pb_file_ft8006s_an,
    .pb_length = sizeof(pb_file_ft8006s_an),
    .upgrade = fts_ft8006s_an_upgrade,
    .get_hlic_ver = fts_ft8006s_an_get_hlic_ver,
    .lic_upgrade = fts_ft8006s_an_lic_upgrade,
};
