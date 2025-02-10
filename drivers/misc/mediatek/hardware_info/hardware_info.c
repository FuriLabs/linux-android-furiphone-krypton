#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/poll.h>
#include <linux/fb.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>

#include<linux/timer.h>
#include<linux/jiffies.h>

#include "hardware_info.h"
#include <linux/fs.h>

int len = 0;

struct class *hardware_info_class;

struct hardware_info current_lcm_info =
{
	"unknown","unknown","unknown","unknown",
};

struct hardware_info current_camera_info[5] =
{
	{"unknown","unknown","unknown","unknown"},
	{"unknown","unknown","unknown","unknown"},
	{"unknown","unknown","unknown","unknown"},
	{"unknown","unknown","unknown","unknown"},
	{"unknown","unknown","unknown","unknown"},
};
struct hardware_info current_tp_info =
{
	"unknown","unknown","unknown","unknown",
};

struct hardware_info current_wireless_info =
{
	"unknown","unknown","unknown","unknown",
};

struct hardware_info current_alsps_info =
{
	"unknown","unknown","unknown","unknown",
};

struct hardware_info current_gsensor_info =
{
	"unknown","unknown","unknown","unknown",
};
struct hardware_info current_msensor_info =
{
	"unknown","unknown","unknown","unknown",
};
struct hardware_info current_barosensor_info =
{
	"unknown","unknown","unknown","unknown",
};

struct hardware_info current_fingerprint_info =
{
	"unknown","unknown","unknown","unknown",
};
struct hardware_info current_coulo_info =
{
	"unknown","unknown","unknown","unknown",
};

struct hardware_info current_mmc_info =
{
	"unknown","unknown","unknown","unknown",
};

struct hardware_info current_sarsensor_info =
{
	"unknown","unknown","unknown","unknown",
};

EXPORT_SYMBOL_GPL(current_lcm_info);
EXPORT_SYMBOL_GPL(current_camera_info);
EXPORT_SYMBOL_GPL(current_tp_info);
EXPORT_SYMBOL_GPL(current_alsps_info);
EXPORT_SYMBOL_GPL(current_gsensor_info);
EXPORT_SYMBOL_GPL(current_msensor_info);
EXPORT_SYMBOL_GPL(current_barosensor_info);
EXPORT_SYMBOL_GPL(current_fingerprint_info);
EXPORT_SYMBOL_GPL(current_sarsensor_info);

static void dev_get_current_lcm_info(char *buf)
{
	char *p = buf;

	if (strcmp(current_lcm_info.chip, "unknown") == 0)
	 	return ;

	 p += sprintf(p, "[LCM]:\n");
	 p += sprintf(p, "chip: %s\n", current_lcm_info.chip);
	 p += sprintf(p, "id: %s\n", current_lcm_info.id);
	 p += sprintf(p, "vendor: %s\n",current_lcm_info.vendor);
	 p += sprintf(p, "more: %s\n", current_lcm_info.more);

	 len += (p - buf);
}
static void dev_get_current_camera_info(char *buf)
{
	char *p = buf;

	if (strcmp(current_camera_info[0].chip, "unknown") != 0) {
		p += sprintf(p, "\n[main camera]:\n");
		p += sprintf(p, "chip: %s\n", current_camera_info[0].chip);
		p += sprintf(p, "id: %s\n", current_camera_info[0].id);
		p += sprintf(p, "vendor: %s\n",current_camera_info[0].vendor);
		p += sprintf(p, "more: %s\n", current_camera_info[0].more);
	}

	if (strcmp(current_camera_info[1].chip, "unknown") != 0) {
		p += sprintf(p, "\n[front camera]:\n");
		p += sprintf(p, "chip:%s\n", current_camera_info[1].chip);
		p += sprintf(p, "id:%s\n", current_camera_info[1].id);
		p += sprintf(p, "vendor:%s\n",current_camera_info[1].vendor);
		p += sprintf(p, "more:%s\n", current_camera_info[1].more);
	}

	if (strcmp(current_camera_info[2].chip,"unknown") != 0) {
		p += sprintf(p, "\n[macro camera]:\n");
		p += sprintf(p, "chip: %s\n", current_camera_info[2].chip);
		p += sprintf(p, "id: %s\n", current_camera_info[2].id);
		p += sprintf(p, "vendor: %s\n",current_camera_info[2].vendor);
		p += sprintf(p, "more: %s\n", current_camera_info[2].more);
	}

	len += (p - buf);
}

#if defined(CONFIG_PRIZE_MT5725_SUPPORT_15W)
static void dev_get_wireless_version(char *buf)
{
	char *p = buf;

	if (strcmp(current_wireless_info.chip,"unknown") == 0)
		return ;

	p += sprintf(p, "\n[wireless]:\n");
	p += sprintf(p, "chip: %s\n", current_wireless_info.chip);
	p += sprintf(p, "id: %s\n", current_wireless_info.id);
	p += sprintf(p, "vendor: %s\n",current_wireless_info.vendor);
	p += sprintf(p, "more: %s\n", current_wireless_info.more);

	len += (p - buf);
}
#endif

static void  dev_get_current_tp_info(char *buf)
{
	char *p = buf;

	if (strcmp(current_tp_info.chip, "unknown") == 0)
		return ;

	 p += sprintf(p, "\n[Touch Panel]:\n");
	 p += sprintf(p, "chip: %s\n", current_tp_info.chip);
	 p += sprintf(p, "id: %s\n", current_tp_info.id);
	 p += sprintf(p, "vendor: %s\n",current_tp_info.vendor);
	 p += sprintf(p, "more: %s\n", current_tp_info.more);

	 len += (p - buf);
}

static void dev_get_current_alsps_info(char *buf)
{
	char *p = buf;

	if (strcmp(current_alsps_info.chip, "unknown") == 0)
		return ;

	p += sprintf(p, "\n[ALS]:\n");
	p += sprintf(p, "chip: %s\n", current_alsps_info.chip);
	p += sprintf(p, "id: %s\n", current_alsps_info.id);
	p += sprintf(p, "vendor: %s\n",current_alsps_info.vendor);
	p += sprintf(p, "more: %s\n", current_alsps_info.more);

	len += (p - buf);
}

static void dev_get_current_gsensor_info(char *buf)
{
	char *p = buf;

	if (strcmp(current_gsensor_info.chip, "unknown") == 0)
	 	return;

	p += sprintf(p, "\n[Accelerometer sensor]:\n");
	p += sprintf(p, "chip: %s\n", current_gsensor_info.chip);
	p += sprintf(p, "id: %s\n", current_gsensor_info.id);
	p += sprintf(p, "vendor: %s\n",current_gsensor_info.vendor);
	p += sprintf(p, "more: %s\n", current_gsensor_info.more);

	len += (p - buf);
}

static void dev_get_current_barosensor_info(char *buf)
{
	char *p = buf;
	if (strcmp(current_barosensor_info.chip, "unknown") == 0)
	 	return ;

	p += sprintf(p, "\n[Barometer sensor]:\n");
	p += sprintf(p, "chip: %s\n", current_barosensor_info.chip);
	p += sprintf(p, "id: %s\n", current_barosensor_info.id);
	p += sprintf(p, "vendor: %s\n",current_barosensor_info.vendor);
	p += sprintf(p, "more: %s\n", current_barosensor_info.more);

	len += (p - buf);
}

static void dev_get_current_msensor_info(char *buf)
{
	char *p = buf;

	if (strcmp(current_msensor_info.chip, "unknown") == 0)
	 	return;

	p += sprintf(p, "\n[Magnetic sensor]:\n");
	p += sprintf(p, "chip: %s\n", current_msensor_info.chip);
	p += sprintf(p, "id: %s\n", current_msensor_info.id);
	p += sprintf(p, "vendor: %s\n",current_msensor_info.vendor);
	p += sprintf(p, "more: %s\n", current_msensor_info.more);

	len += (p - buf);
}

static void dev_get_current_fingerprint_info(char *buf)
{
	char *p = buf;

	if (strcmp(current_fingerprint_info.chip, "unknown") == 0)
		return;

	p += sprintf(p, "\n[Fingerprint]:\n");
	p += sprintf(p, "chip: %s\n", current_fingerprint_info.chip);
	p += sprintf(p, "id: %s\n", current_fingerprint_info.id);
	p += sprintf(p, "vendor: %s\n",current_fingerprint_info.vendor);
	p += sprintf(p, "more: %s\n", current_fingerprint_info.more);

	len += (p - buf);
}

static void dev_get_current_coulo_info(char *buf)
{
	char *p = buf;

	if (strcmp(current_coulo_info.chip, "unknown") == 0)
		return ;

	p += sprintf(p, "\n[coulombmeter]:\n");
	p += sprintf(p, "chip: %s\n", current_coulo_info.chip);
	p += sprintf(p, "id: %s\n", current_coulo_info.id);
	p += sprintf(p, "vendor: %s\n",current_coulo_info.vendor);
	p += sprintf(p, "more: %s\n", current_coulo_info.more);

	len += (p - buf);
}

static ssize_t hardware_info_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	len = 0;

	dev_get_current_lcm_info(buf + len);

	dev_get_current_camera_info(buf + len);

	dev_get_current_tp_info(buf + len);

	dev_get_current_alsps_info(buf + len);

	dev_get_current_gsensor_info(buf + len);

	dev_get_current_msensor_info(buf + len);

	dev_get_current_barosensor_info(buf + len);

	dev_get_current_fingerprint_info(buf + len);

	dev_get_current_coulo_info(buf + len);

#if defined(CONFIG_PRIZE_MT5725_SUPPORT_15W)
	dev_get_wireless_version(buf + len);
#endif

	return len;
}

static DEVICE_ATTR(hw_info_read, 0664, hardware_info_show, NULL);

static int __init hardware_info_dev_init(void)
{
	struct device *hardware_info_dev;
	hardware_info_class = class_create(THIS_MODULE, "hw_info");

	if (IS_ERR(hardware_info_class)) {
		pr_debug("Failed to create class(hardware_info)!");
		return PTR_ERR(hardware_info_class);
	}

	hardware_info_dev = device_create(hardware_info_class, NULL, 0, NULL, "hw_info_data");
	if (IS_ERR(hardware_info_dev))
		pr_debug("Failed to create hardware_info_dev device");

	if (device_create_file(hardware_info_dev, &dev_attr_hw_info_read) < 0)
		pr_debug("Failed to create device file(%s)!",dev_attr_hw_info_read.attr.name);

	return 0;
}

static void __exit hardware_info_dev_exit(void)
{
	class_destroy(hardware_info_class);
}

module_init (hardware_info_dev_init);
module_exit(hardware_info_dev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lixuefeng <lixuefeng@boruizhiheng.com>");
MODULE_DESCRIPTION("show hardware info Driver");
