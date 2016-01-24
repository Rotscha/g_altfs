KERNEL_DIR=~/src/pandora-kernel

obj-m       += g_altfs_mod.o
g_altfs_mod-objs := g_altfs.o g_altfs_node.o g_altfs_gadget.o composite.o usbstring.o epautoconf.o config.o

PWD := $(shell pwd)

default:
	$(MAKE) ARCH=arm CROSS_COMPILE=arm-none-linux-gnueabi- -C $(KERNEL_DIR) SUBDIRS=$(PWD) modules
clean:
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(PWD) clean

