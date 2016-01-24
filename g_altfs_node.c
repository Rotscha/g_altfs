#include <linux/module.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>

#include "g_altfs.h"
#include "g_altfs_node.h"
#include "g_altfs_gadget.h"

#define DEVICE_NAME "g_altfs"

#define G_ALTFS_IOCTL_START _IO('g', 1)
#define G_ALTFS_IOCTL_STOP  _IO('g', 2)


int     g_altfs_node_open(struct inode *inode, struct file *file);
int     g_altfs_node_release(struct inode *inode, struct file *file);
ssize_t g_altfs_node_read(struct file *filp, char *buffer, size_t length, loff_t * offset);
ssize_t g_altfs_node_write(struct file *filp, const char *buff, size_t len, loff_t * off);
long    g_altfs_node_ioctl(struct file *fd, unsigned code, unsigned long value);

struct file_operations fileOps =
{
    .read    = g_altfs_node_read,
    .write   = g_altfs_node_write,
    .open    = g_altfs_node_open,
    .release = g_altfs_node_release,
    .unlocked_ioctl = g_altfs_node_ioctl
};

struct cdev     cdev;
dev_t           devNo;
struct class *  devClass;
struct device * dev = NULL;

int g_altfs_node_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "device open\n");

    return 0;
}


int g_altfs_node_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "device release\n");

    return 0;
}

ssize_t g_altfs_node_read(struct file *filp, char *buffer, size_t length, loff_t * offset)
{
//    printk(KERN_INFO "device read %i\n", length);

    return g_altfs_gadget_read_buf(buffer, length);
}

ssize_t g_altfs_node_write(struct file *filp, const char *buff, size_t len, loff_t * off)
{
//    printk(KERN_INFO "device write\n");

    return g_altfs_gadget_write_buf(buff, len);
}

// *********************************************************************************

long g_altfs_node_ioctl(struct file *fd, unsigned code, unsigned long value)
{
    if (code == G_ALTFS_IOCTL_START)
    {
        return g_altfs_gadget_start();
    }

    if (code == G_ALTFS_IOCTL_STOP)
    {
        g_altfs_gadget_stop();
    }

    return -1;
}



int g_altfs_node_create(void)
{
    int            ret;

    ret = alloc_chrdev_region(&devNo, 0, 1, DEVICE_NAME);

    if (ret < 0)
    {
        printk(KERN_WARNING "err alloc_chrdev_region\n");

        return ret;
    }

    devClass = class_create(THIS_MODULE, DEVICE_NAME);

    if (IS_ERR(devClass))
    {
        ret = PTR_ERR(devClass);

        printk(KERN_WARNING "err class_create\n");

        unregister_chrdev_region(devNo, 1);

        return ret;
    }

    cdev_init(&cdev, &fileOps);
    
    ret = cdev_add(&cdev, devNo, 1);

    if (ret)
    {
        printk(KERN_WARNING "err cdev_add\n");

        class_destroy(devClass);
        unregister_chrdev_region(devNo, 1);

        return ret;
    }

    dev = device_create(devClass, NULL, devNo, NULL, DEVICE_NAME);

    if (IS_ERR(dev))
    {
        ret = PTR_ERR(dev);

        printk(KERN_WARNING "err device_create\n");

        cdev_del(&cdev);

        return ret;
    }

    return 0;
}


void g_altfs_node_destroy()
{
    device_destroy(devClass, devNo);
    cdev_del(&cdev);
    class_destroy(devClass);
    unregister_chrdev_region(devNo, 1);
}
