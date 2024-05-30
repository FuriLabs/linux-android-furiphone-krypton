#define DEBUG
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/of_gpio.h>
#ifndef LEGACY
#include <linux/workqueue.h>
#include <linux/acpi.h>
#include <linux/gpio/consumer.h>
#include <net/nfc/nci.h>
#include <linux/clk.h>
#else
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_address.h>
#endif
#include <linux/of_irq.h>
#include "ttmdev.h"


#ifdef CONFIG_MTK_I2C_EXTENSION
#define KRNMTKLEGACY_GPIO 1
#endif

//#define KRNMTKLEGACY_GPIO 1

#define MAX_BUFFER_SIZE 260
#define HEADER_LENGTH 3
#define IDLE_CHARACTER 0x7e
#define TTMDEV_POWER_STATE_MAX 3
#define WAKEUP_SRC_TIMEOUT (500)

#define DRIVER_VERSION "2.2.0.15"

#define I2C_ID_NAME "ttmdev"


static bool enable_debug_log;

struct ttmdev_device {
	struct mutex read_mutex;
	struct i2c_client *client;
	struct miscdevice ttmdev_device;
	uint8_t buffer[MAX_BUFFER_SIZE];
	bool device_open;
	u8 *bus_tx_buf;
    u8 *bus_rx_buf;
	struct mutex bus_lock;
};

struct ttmdev_device *ttm_data;

#define I2C_RETRY_NUMBER                    3
#define I2C_BUF_LENGTH                      256


//prize add by lipengpeng 20211125 start 

/*static char i2c_read_reg(struct i2c_client *client, u8 addr)
{
	char ret;
	u8 rdbuf[2] = {0};
	struct ttmdev_device *ttm_data_read = ttm_data;
	u8 beg = addr;
	
   // u8 buffer[2]={0};
	
    //buffer[0] = (addr >> 8) & 0xFF;
    //buffer[1] = addr & 0xFF;
	
    mutex_lock(&ttm_data_read->bus_lock);	
	
	struct i2c_msg msgs[] = {
		{
			.addr	= client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= &beg,
		},
		{
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= rdbuf,
		},
	};
	
	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret < 0)
		pr_err("msg %s i2c read error: %d\n", __func__, ret);
	
	mutex_unlock(&ttm_data_read->bus_lock);
    return rdbuf[0];
}

static s32 chipid_i2c_read(u8 addr)
{
	char get_byte=0;
   
	get_byte = i2c_read_reg(ttm_data->client, addr);
    return get_byte;
}
*/
int ttmfts_read(u16 cmd, u32 cmdlen, u8 *data, u32 datalen) //7bit 0x3C  read:0x78/write:0x79
{
    int ret = 0;
    int i = 0;
    struct ttmdev_device *ttm_data_read = ttm_data;
    struct i2c_msg msg_list[2];
    struct i2c_msg *msg = NULL;
    int msg_num = 0;
	u8 buffer[2]={0};

	//struct ttmdev_device *ttmdev_dev =container_of(filp->private_data,struct ttmdev_device, ttmdev_device);
			
    /* must have data when read */
    if (!ttm_data_read || !ttm_data_read->client || !data || !datalen
        || (datalen >= I2C_BUF_LENGTH) || (cmdlen >= I2C_BUF_LENGTH)) {
        printk("ttm_data_read/client/cmdlen(%d)/data/datalen(%d) data=%d  ttm_data_read->client= %d ttm_data_read=%d is invalid",
                  cmdlen, datalen, data, ttm_data_read->client, ttm_data_read);
        return -EINVAL;
    }
	
	buffer[0] =  (cmd & 0xFF00)>>8;
    buffer[1] =   cmd & 0x00FF;

    mutex_lock(&ttm_data_read->bus_lock);
    memset(&msg_list[0], 0, sizeof(struct i2c_msg));
    memset(&msg_list[1], 0, sizeof(struct i2c_msg));
    memcpy(ttm_data_read->bus_tx_buf, buffer, cmdlen);
    msg_list[0].addr = ttm_data_read->client->addr;
    msg_list[0].flags = 0;
    msg_list[0].len = cmdlen;
    msg_list[0].buf = ttm_data_read->bus_tx_buf;
    msg_list[1].addr = ttm_data_read->client->addr;
    msg_list[1].flags = I2C_M_RD;
    msg_list[1].len = datalen;
    msg_list[1].buf = ttm_data_read->bus_rx_buf;
    //if (cmd && cmdlen) {
        msg = &msg_list[0];
        msg_num = 2;
    //} else {
     //   msg = &msg_list[1];
     //   msg_num = 1;
    //}

    for (i = 0; i < I2C_RETRY_NUMBER; i++) {
        ret = i2c_transfer(ttm_data_read->client->adapter, msg, msg_num);
        if (ret < 0) {
            printk("i2c_transfer(read) fail,ret:%d", ret);
        } else {
            memcpy(data, ttm_data_read->bus_rx_buf, datalen);
            break;
        }
    }

    mutex_unlock(&ttm_data_read->bus_lock);
    return ret;
}

int ttmdev_read_reg(u16 addr, u8 *value)
{
    return ttmfts_read(addr, 2, value, 1);
}

//prize add by lipengpeng 20211125 end 

int ttm_read(u8 *cmd, u32 cmdlen, u8 *data, u32 datalen)
{
    int ret = 0;
    int i = 0;
    struct ttmdev_device *ttm_data = ttm_data;
    struct i2c_msg msg_list[2];
    struct i2c_msg *msg = NULL;
    int msg_num = 0;

    if (!ttm_data || !ttm_data->client || !data || !datalen
        || (datalen >= I2C_BUF_LENGTH) || (cmdlen >= I2C_BUF_LENGTH)) {
        printk("ttm_data/client/cmdlen(%d)/data/datalen(%d) is invalid",
                  cmdlen, datalen);
        return -EINVAL;
    }

    mutex_lock(&ttm_data->bus_lock);
    memset(&msg_list[0], 0, sizeof(struct i2c_msg));
    memset(&msg_list[1], 0, sizeof(struct i2c_msg));
    memcpy(ttm_data->bus_tx_buf, cmd, cmdlen);
    msg_list[0].addr = ttm_data->client->addr;
    msg_list[0].flags = 0;
    msg_list[0].len = cmdlen;
    msg_list[0].buf = ttm_data->bus_tx_buf;
    msg_list[1].addr = ttm_data->client->addr;
    msg_list[1].flags = I2C_M_RD;
    msg_list[1].len = datalen;
    msg_list[1].buf = ttm_data->bus_rx_buf;
    if (cmd && cmdlen) {
        msg = &msg_list[0];
        msg_num = 2;
    } else {
        msg = &msg_list[1];
        msg_num = 1;
    }

    for (i = 0; i < I2C_RETRY_NUMBER; i++) {
        ret = i2c_transfer(ttm_data->client->adapter, msg, msg_num);
        if (ret < 0) {
            printk("i2c_transfer(read) fail,ret:%d", ret);
        } else {
            memcpy(data, ttm_data->bus_rx_buf, datalen);
            break;
        }
    }

    mutex_unlock(&ttm_data->bus_lock);
    return ret;
}

int ttm_write(const char *writebuf, u32 writelen)
{
    int ret = 0;
    int i = 0;
    struct ttmdev_device *ttm_data = ttm_data;
    struct i2c_msg msgs;

    if (!ttm_data || !ttm_data->client || !writebuf || !writelen
        || (writelen >= I2C_BUF_LENGTH)) {
        printk("ttm_data/client/data/datalen(%d) is invalid", writelen);
        return -EINVAL;
    }

    mutex_lock(&ttm_data->bus_lock);
    memset(&msgs, 0, sizeof(struct i2c_msg));
    memcpy(ttm_data->bus_tx_buf, writebuf, writelen);
    msgs.addr = ttm_data->client->addr;
    msgs.flags = 0;
    msgs.len = writelen;
    msgs.buf = ttm_data->bus_tx_buf;
    for (i = 0; i < I2C_RETRY_NUMBER; i++) {
        ret = i2c_transfer(ttm_data->client->adapter, &msgs, 1);
        if (ret < 0) {
            printk("i2c_transfer(write) fail,ret:%d", ret);
        } else {
            break;
        }
    }
    mutex_unlock(&ttm_data->bus_lock);
    return ret;
}

//int ttm_read_reg(u8 addr, u8 *value)
//{
 //   return ttm_read(&addr, 1, value, 1);
//}

//int ttm_write_reg(u8 addr, u8 value)
//{
//    u8 buf[2] = { 0 };

//    buf[0] = addr;
//    buf[1] = value;
//    return ttm_write(buf, sizeof(buf));
//}

int ttm_bus_init(struct ttmdev_device *ttm_data)
{

    ttm_data->bus_tx_buf = kzalloc(I2C_BUF_LENGTH, GFP_KERNEL);
    if (NULL == ttm_data->bus_tx_buf) {
        printk("failed to allocate memory for bus_tx_buf");
        return -ENOMEM;
    }

    ttm_data->bus_rx_buf = kzalloc(I2C_BUF_LENGTH, GFP_KERNEL);
    if (NULL == ttm_data->bus_rx_buf) {
        printk("failed to allocate memory for bus_rx_buf");
        return -ENOMEM;
    }

    return 0;
}

int ttm_bus_exit(struct ttmdev_device *ttm_data)
{

    if (ttm_data && ttm_data->bus_tx_buf) {
        kfree(ttm_data->bus_tx_buf);
        ttm_data->bus_tx_buf = NULL;
    }

    if (ttm_data && ttm_data->bus_rx_buf) {
        kfree(ttm_data->bus_rx_buf);
        ttm_data->bus_rx_buf = NULL;
    }

    return 0;
}

static ssize_t ttmdev_dev_read(
	struct file *filp, char __user *buf, size_t count, loff_t *offset)
{
	struct ttmdev_device *ttmdev_dev =
		container_of(filp->private_data,
			struct ttmdev_device, ttmdev_device);
	int ret;
   // return 0;
	if (count == 0)
		return 0;

	if (count > MAX_BUFFER_SIZE)
		count = MAX_BUFFER_SIZE;

	if (enable_debug_log)
		pr_debug("%s : reading %zu bytes.\n", __func__, count);
	
	mutex_lock(&ttmdev_dev->read_mutex);

	ret = i2c_master_recv(ttmdev_dev->client, ttmdev_dev->buffer, count);

	mutex_unlock(&ttmdev_dev->read_mutex);

	if (ret < 0) {
		pr_err("%s: i2c_master_recv returned %d\n", __func__, ret);
		return ret;
	}
	if (ret > count) {
		pr_err("%s: received too many bytes from i2c (%d)\n",
			__func__, ret);
		return -EIO;
	}

	if (copy_to_user(buf, ttmdev_dev->buffer, ret)) {
		pr_warn("%s : failed to copy to user space\n", __func__);
		return -EFAULT;
	}

	return ret;
}

static ssize_t ttmdev_dev_write(struct file *filp, const char __user *buf,
	size_t count, loff_t *offset)
{
	struct ttmdev_device *ttmdev_dev =
		container_of(filp->private_data,
			struct ttmdev_device, ttmdev_device);
	char *tmp = NULL;
	int ret = count;

	if (enable_debug_log) {
		pr_debug("%s : writing %zu bytes.\n", __func__, count);
	}

	if (count > MAX_BUFFER_SIZE)
		count = MAX_BUFFER_SIZE;

	tmp = memdup_user(buf, count);
	if (IS_ERR_OR_NULL(tmp)) {
		pr_err("%s : memdup_user failed\n", __func__);
		return -EFAULT;
	}

	ret = i2c_master_send(ttmdev_dev->client, tmp, count);

	if (ret != count) {
		pr_err("%s : i2c_master_send returned %d\n", __func__, ret);
		ret = -EIO;
	}
	kfree(tmp);

	return ret;
}

static int ttmdev_dev_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	struct ttmdev_device *ttmdev_dev =
		container_of(filp->private_data,
			struct ttmdev_device, ttmdev_device);

	if (enable_debug_log)
		pr_info("%s:%d dev_open", __FILE__, __LINE__);

	if (ttmdev_dev->device_open) {
		ret = -EBUSY;
		pr_err("%s : device already opened ret= %d\n", __func__, ret);
	} else {
		ttmdev_dev->device_open = true;
	}
	return ret;
}

static int ttmdev_release(struct inode *inode, struct file *file)
{
	struct ttmdev_device *ttmdev_dev =
		container_of(file->private_data,
			struct ttmdev_device, ttmdev_device);

	ttmdev_dev->device_open = false;
	if (enable_debug_log)
		pr_debug("%s : device_open  = false\n", __func__);

	return 0;
}


static long ttmdev_dev_ioctl(struct file *filp,
	unsigned int cmd, unsigned long arg)
{
	//struct ttmdev_device *ttmdev_dev =
	//	container_of(filp->private_data,
	//		struct ttmdev_device, ttmdev_device);

	int ret = 0;

	u32 tmp;

	if (_IOC_TYPE(cmd) != TTMDEV_MAGIC)
		return -ENOTTY;


	if (_IOC_DIR(cmd) & _IOC_READ)
		ret = !ACCESS_OK(VERIFY_WRITE,
			(void __user *)arg, _IOC_SIZE(cmd));
	if (ret == 0 && _IOC_DIR(cmd) & _IOC_WRITE)
		ret = !ACCESS_OK(VERIFY_READ,
			(void __user *)arg, _IOC_SIZE(cmd));
	if (ret)
		return -EFAULT;

	switch (cmd) {
	case TTMDEV_SET_POLARITY_RISING:
	case TTMDEV_LEGACY_SET_POLARITY_RISING:
		pr_info(" ### TTMDEV_SET_POLARITY_RISING ###\n");
		//ttmdev_loc_set_polaritymode(ttmdev_dev, IRQF_TRIGGER_RISING);
		break;

	case TTMDEV_SET_POLARITY_HIGH:
	case TTMDEV_LEGACY_SET_POLARITY_HIGH:
		pr_info(" ### TTMDEV_SET_POLARITY_HIGH ###\n");
		//ttmdev_loc_set_polaritymode(ttmdev_dev, IRQF_TRIGGER_HIGH);
		break;

	case TTMDEV_PULSE_RESET:
	case TTMDEV_LEGACY_PULSE_RESET:
		pr_info("%s Double Pulse Request\n", __func__);
		break;

	case TTMDEV_GET_WAKEUP:
	case TTMDEV_LEGACY_GET_WAKEUP:
		if (ret != 0)
			ret = 1;

		if (enable_debug_log)
			pr_debug("%s get gpio result %d\n", __func__, ret);
		break;
	case TTMDEV_GET_POLARITY:
	case TTMDEV_LEGACY_GET_POLARITY:
		if (enable_debug_log)
			pr_debug("%s get polarity %d\n", __func__, ret);
		break;
	case TTMDEV_RECOVERY:
	case TTMDEV_LEGACY_RECOVERY:
		pr_info("%s Recovery Request\n", __func__);
		break;
		
	case TTMDEV_USE_ESE:
		ret = __get_user(tmp, (u32 __user *)arg);
		if (ret == 0) {
         printk("lpp---test===\n");
		}
		if (enable_debug_log)
			pr_debug("%s use ESE %d : %d\n", __func__, ret, tmp);
		break;
	default:
		pr_err("%s bad ioctl %u\n", __func__, cmd);
		ret = -EINVAL;
		break;
	}
	return ret;
}

#ifndef KRNMTKLEGACY_GPIO
static int ttmdev_platform_probe(struct platform_device *pdev)
{
	if (enable_debug_log)
		pr_debug("%s\n", __func__);

	return 0;
}

static int ttmdev_platform_remove(struct platform_device *pdev)
{
	if (enable_debug_log)
		pr_debug("%s\n", __func__);

	return 0;
}
#endif /* KRNMTKLEGACY_GPIO */

static const struct file_operations ttmdev_dev_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.read = ttmdev_dev_read,
	.write = ttmdev_dev_write,
	.open = ttmdev_dev_open,
	.release = ttmdev_release,
	.unlocked_ioctl = ttmdev_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = ttmdev_dev_ioctl
#endif
};

static ssize_t i2c_addr_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	 u8 ttmdev_fw_version=0;
//	uint8_t chip_id =0;
    int ret = 0;
	struct i2c_client *client = to_i2c_client(dev);


    ret = ttmdev_read_reg(0x0001, &ttmdev_fw_version); 
	
//	chip_id = chipid_i2c_read(0x01);
	
 //   printk("lpp read id  read %x\n",chip_id);
	
	if (client != NULL)
	{
		printk("client=null\n");
		return scnprintf(buf, PAGE_SIZE, "0x%.2x\n", ttmdev_fw_version);
	}
	else{
		printk("client=null no\n");
		return scnprintf(buf, PAGE_SIZE, "0x%.2x\n", client->addr);
	}
	
	return -ENODEV;
} /* i2c_addr_show() */

static ssize_t i2c_addr_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct ttmdev_device *data = dev_get_drvdata(dev);
	long new_addr = 0;

	if (data != NULL && data->client != NULL) {
		if (!kstrtol(buf, 10, &new_addr)) {
			mutex_lock(&data->read_mutex);
			data->client->addr = new_addr;
			mutex_unlock(&data->read_mutex);
			return count;
		}
		return -EINVAL;
	}
	return 0;
}

static ssize_t read_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", DRIVER_VERSION);
} 

static ssize_t write_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t count)
{
	struct ttmdev_device *data = dev_get_drvdata(dev);
	long new_addr = 0;

	if (data != NULL && data->client != NULL) {
		if (!kstrtol(buf, 10, &new_addr)) {
			ttm_write(buf,count);
			return count;
		}
		return -EINVAL;
	}
	return 0;


}
static ssize_t write_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (client != NULL)
		return scnprintf(buf, PAGE_SIZE, "0x%.2x\n", client->addr);
	return -ENODEV;
} 


static DEVICE_ATTR_RW(i2c_addr);

static DEVICE_ATTR_RO(read);

static DEVICE_ATTR_RW(write);

static struct attribute *ttmdev_attrs[] = {
	&dev_attr_i2c_addr.attr,
	&dev_attr_read.attr,
	&dev_attr_write.attr,
	NULL,
};

static struct attribute_group ttmdev_attr_grp = {
	.attrs = ttmdev_attrs,
};


static int ttmdev_probe(struct i2c_client *client,
						 const struct i2c_device_id *id)
{
	int ret;
	struct ttmdev_device *ttmdev_dev;
	struct device *dev = &client->dev;
//	u8 ttmdev_fw_version=0;


    printk("lpp----ttmdev probe start 111\n");
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s : need I2C_FUNC_I2C\n", __func__);
		return -ENODEV;
	}

	ttmdev_dev = devm_kzalloc(dev, sizeof(*ttmdev_dev), GFP_KERNEL);
	if (ttmdev_dev == NULL)
		return -ENOMEM;

    printk("lpp----ttmdev probe start 222\n");
	
//prize add by lipengpeng 20211125 start 
	ttm_data = ttmdev_dev;
//prize add by lipengpeng 20211125 end 	
	ttmdev_dev->client = client;
	client->adapter->retries = 0;

	ttmdev_dev->ttmdev_device.minor = MISC_DYNAMIC_MINOR;
	ttmdev_dev->ttmdev_device.name = "ttmdev";
	ttmdev_dev->ttmdev_device.fops = &ttmdev_dev_fops;
	ttmdev_dev->ttmdev_device.parent = dev;

	i2c_set_clientdata(client, ttmdev_dev);

	ret = ttm_bus_init(ttmdev_dev);
    if (ret) {
        printk("ttm lpp bus initialize fail");
    }
	printk("lpp----ttmdev probe start 333\n");

    //ret = ttmdev_read_reg(0x01, &ttmdev_fw_version); 
	
    //printk("lpp read id  read:%x",ttmdev_fw_version);
	
	ret = misc_register(&ttmdev_dev->ttmdev_device);
	if (ret) {
		pr_err("%s : misc_register failed\n", __func__);
		goto err_misc_register;
	}
    
	ret = sysfs_create_group(&dev->kobj, &ttmdev_attr_grp);
	if (ret) {
		pr_err("%s : sysfs_create_group failed\n", __func__);
		goto err_sysfs_create_group_failed;
	}
     printk("lpp----ttmdev probe end 444\n");
	return 0;

err_sysfs_create_group_failed:
	misc_deregister(&ttmdev_dev->ttmdev_device);
err_misc_register:
	mutex_destroy(&ttmdev_dev->read_mutex);

	return ret;
}

static int ttmdev_remove(struct i2c_client *client)
{
	struct ttmdev_device *ttmdev_dev = i2c_get_clientdata(client);

	misc_deregister(&ttmdev_dev->ttmdev_device);
	ttm_bus_exit(ttmdev_dev);

	sysfs_remove_group(&client->dev.kobj, &ttmdev_attr_grp);
	mutex_destroy(&ttmdev_dev->read_mutex);

	return 0;
}

static int ttmdev_suspend(struct device *device)
{
	//struct i2c_client *client = to_i2c_client(device);
	//struct ttmdev_device *ttmdev_dev = i2c_get_clientdata(client);

	return 0;
}

static int ttmdev_resume(struct device *device)
{
	//struct i2c_client *client = to_i2c_client(device);
	//struct ttmdev_device *ttmdev_dev = i2c_get_clientdata(client);

	return 0;
}

static const struct i2c_device_id ttmdev_id[] = {{"ttmdev_iic", 0}, {} };

static const struct of_device_id ttmdev_of_match[] = {
	{	.compatible = "mediatek,ttmdev_iic" },
	{ } };
MODULE_DEVICE_TABLE(of, ttmdev_of_match);

static const struct dev_pm_ops ttmdev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ttmdev_suspend, ttmdev_resume)};


static struct i2c_driver ttmdev_driver = {
	.id_table = ttmdev_id,
	.probe = ttmdev_probe,
	.remove = ttmdev_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = I2C_ID_NAME,
		.of_match_table = ttmdev_of_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.pm = &ttmdev_pm_ops,
		},
};

#ifndef KRNMTKLEGACY_GPIO
/*  platform driver */
static const struct of_device_id nfc_dev_of_match[] = {
	{
		.compatible = "mediatek,ttmdev_platform",
	},
	{ },
};

static struct platform_driver ttmdev_platform_driver = {
	.probe = ttmdev_platform_probe,
	.remove = ttmdev_platform_remove,
	.driver = {
			.name = I2C_ID_NAME,
			.owner = THIS_MODULE,
			.of_match_table = nfc_dev_of_match,
		},
};
#endif


static int __init ttmdev_dev_init(void)
{
	pr_info("Loading ttmdev driver\n");
#ifndef KRNMTKLEGACY_GPIO
	platform_driver_register(&ttmdev_platform_driver);
	if (enable_debug_log)
		pr_debug("Loading ttmdev i2c driver\n");
#endif
	return i2c_add_driver(&ttmdev_driver);
}

module_init(ttmdev_dev_init);

static void __exit ttmdev_dev_exit(void)
{
	pr_info("Unloading ttmdev driver\n");
	i2c_del_driver(&ttmdev_driver);
}

module_exit(ttmdev_dev_exit);

MODULE_AUTHOR("lipengpeng");
MODULE_DESCRIPTION("TTM TTMDEV driver");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
