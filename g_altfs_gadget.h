#ifndef __G_ALTFS_GADGET_H__
#define __G_ALTFS_GADGET_H__

#include <linux/types.h>

long g_altfs_gadget_start(void);
void g_altfs_gadget_stop(void);
ssize_t g_altfs_gadget_write_buf(const char * buf, size_t len);
ssize_t g_altfs_gadget_read_buf(void * buf, size_t len);

#endif 
