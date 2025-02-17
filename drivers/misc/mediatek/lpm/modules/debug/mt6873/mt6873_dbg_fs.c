// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/of_device.h>

#include <mtk_dbg_common_v1.h>
#include <mtk_lpm_module.h>
#include <mt6873_dbg_fs_common.h>

static void __exit mt6873_dbg_fs_exit(void)
{
	mt6873_dbg_lpm_fs_deinit();
	mt6873_dbg_spm_fs_deinit();
	mt6873_dbg_lpm_deinit();
	mtk_dbg_common_fs_exit();
}

static int __init mt6873_dbg_fs_init(void)
{
	mt6873_dbg_lpm_init();
	mt6873_dbg_lpm_fs_init();
	mt6873_dbg_spm_fs_init();
	mtk_dbg_common_fs_init();
	pr_debug("%s %d: finish", __func__, __LINE__);
	return 0;

}

module_init(mt6873_dbg_fs_init);
module_exit(mt6873_dbg_fs_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT6873 Low Power FileSystem");
MODULE_AUTHOR("MediaTek Inc.");
