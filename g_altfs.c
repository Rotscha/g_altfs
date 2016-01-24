#include <linux/module.h>
#include "g_altfs_node.h"
#include "g_altfs_gadget.h"


int g_altfs_init(void)
{
    int ret;

    printk(KERN_INFO "g_altfs loaded\n");

    ret = g_altfs_node_create();

    if (ret != 0)
    {
        return ret;
    }

    return 0;
}


void g_altfs_exit(void)
{
    g_altfs_gadget_stop();
    g_altfs_node_destroy();

    printk(KERN_INFO "g_altfs unloaded\n");
}


int __init init(void)
{
    return g_altfs_init();
}


void __exit cleanup(void)
{
    g_altfs_exit();
}

module_init(init);
module_exit(cleanup);

MODULE_AUTHOR("Roger Zoellner");
MODULE_LICENSE("GPL");
