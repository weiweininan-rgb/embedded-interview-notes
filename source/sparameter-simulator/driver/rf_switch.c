// SPDX-License-Identifier: GPL-2.0
/*
 * GPIO consumer for the future RF calibration/DUT path switch.
 *
 * This is a Stage 03 source/design exercise.  It requires a future board to
 * supply its actual GPIO polarity, selector-bit order and safe path in DTS.
 */
#include "rf_switch_uapi.h"

#include <linux/cdev.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define RF_SWITCH_SELECT_LINES 3U

struct rf_switch_device {
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct gpio_descs *select;
    struct mutex lock;
    dev_t devt;
    u32 path;
    u32 safe_path;
};

static int rf_switch_apply_path(struct rf_switch_device *switch_device, u32 path)
{
    unsigned int index;

    if (path > RF_SWITCH_PATH_MAX)
        return -EINVAL;

    /* select-gpios[0] is the least-significant selector bit by DTS contract. */
    for (index = 0; index < RF_SWITCH_SELECT_LINES; ++index)
        gpiod_set_value_cansleep(switch_device->select->desc[index],
                                 (path >> index) & 1U);

    switch_device->path = path;
    return 0;
}

static long rf_switch_ioctl(struct file *file, unsigned int command,
                            unsigned long argument)
{
    struct rf_switch_device *switch_device = file->private_data;
    u32 path;
    int status;

    switch (command) {
    case RF_SWITCH_SET_PATH:
        if (copy_from_user(&path, (void __user *)argument, sizeof(path)))
            return -EFAULT;

        mutex_lock(&switch_device->lock);
        status = rf_switch_apply_path(switch_device, path);
        mutex_unlock(&switch_device->lock);
        return status;

    case RF_SWITCH_GET_PATH:
        mutex_lock(&switch_device->lock);
        path = switch_device->path;
        mutex_unlock(&switch_device->lock);

        if (copy_to_user((void __user *)argument, &path, sizeof(path)))
            return -EFAULT;
        return 0;

    default:
        return -ENOTTY;
    }
}

static int rf_switch_open(struct inode *inode, struct file *file)
{
    file->private_data = container_of(inode->i_cdev, struct rf_switch_device,
                                      cdev);
    return 0;
}

static const struct file_operations rf_switch_fops = {
    .owner = THIS_MODULE,
    .open = rf_switch_open,
    .unlocked_ioctl = rf_switch_ioctl,
    .llseek = no_llseek,
};

static int rf_switch_probe(struct platform_device *pdev)
{
    struct rf_switch_device *switch_device;
    int status;

    switch_device = devm_kzalloc(&pdev->dev, sizeof(*switch_device), GFP_KERNEL);
    if (!switch_device)
        return -ENOMEM;

    switch_device->select = devm_gpiod_get_array(&pdev->dev, "select",
                                                  GPIOD_OUT_LOW);
    if (IS_ERR(switch_device->select))
        return PTR_ERR(switch_device->select);
    if (switch_device->select->ndescs != RF_SWITCH_SELECT_LINES) {
        dev_err(&pdev->dev, "select-gpios must provide exactly three lines\n");
        return -EINVAL;
    }

    status = of_property_read_u32(pdev->dev.of_node, "safe-path",
                                  &switch_device->safe_path);
    if (status) {
        dev_err(&pdev->dev, "missing safe-path\n");
        return status;
    }
    if (switch_device->safe_path > RF_SWITCH_PATH_MAX) {
        dev_err(&pdev->dev, "invalid safe-path\n");
        return -EINVAL;
    }

    mutex_init(&switch_device->lock);
    status = alloc_chrdev_region(&switch_device->devt, 0, 1, "rf_switch");
    if (status)
        return status;

    cdev_init(&switch_device->cdev, &rf_switch_fops);
    switch_device->cdev.owner = THIS_MODULE;
    status = cdev_add(&switch_device->cdev, switch_device->devt, 1);
    if (status)
        goto unregister_region;

    switch_device->class = class_create(THIS_MODULE, "rf_switch");
    if (IS_ERR(switch_device->class)) {
        status = PTR_ERR(switch_device->class);
        goto delete_cdev;
    }

    switch_device->device = device_create(switch_device->class, &pdev->dev,
                                          switch_device->devt, NULL, "rf_switch");
    if (IS_ERR(switch_device->device)) {
        status = PTR_ERR(switch_device->device);
        goto destroy_class;
    }

    mutex_lock(&switch_device->lock);
    status = rf_switch_apply_path(switch_device, switch_device->safe_path);
    mutex_unlock(&switch_device->lock);
    if (status)
        goto destroy_device;

    platform_set_drvdata(pdev, switch_device);
    dev_info(&pdev->dev, "registered /dev/rf_switch, safe path=%u\n",
             switch_device->safe_path);
    return 0;

destroy_device:
    device_destroy(switch_device->class, switch_device->devt);
destroy_class:
    class_destroy(switch_device->class);
delete_cdev:
    cdev_del(&switch_device->cdev);
unregister_region:
    unregister_chrdev_region(switch_device->devt, 1);
    return status;
}

static int rf_switch_remove(struct platform_device *pdev)
{
    struct rf_switch_device *switch_device = platform_get_drvdata(pdev);

    mutex_lock(&switch_device->lock);
    rf_switch_apply_path(switch_device, switch_device->safe_path);
    mutex_unlock(&switch_device->lock);
    device_destroy(switch_device->class, switch_device->devt);
    class_destroy(switch_device->class);
    cdev_del(&switch_device->cdev);
    unregister_chrdev_region(switch_device->devt, 1);
    dev_info(&pdev->dev, "removed\n");
    return 0;
}

static const struct of_device_id rf_switch_of_match[] = {
    { .compatible = "edu,sparam-rf-switch" },
    { }
};
MODULE_DEVICE_TABLE(of, rf_switch_of_match);

static struct platform_driver rf_switch_driver = {
    .probe = rf_switch_probe,
    .remove = rf_switch_remove,
    .driver = {
        .name = "sparam-rf-switch",
        .of_match_table = rf_switch_of_match,
    },
};
module_platform_driver(rf_switch_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IMX6ULL S-parameter simulator project");
MODULE_DESCRIPTION("GPIO RF path switch source/design reference");
