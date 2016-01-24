#include "g_altfs_gadget.h"
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>
#include <linux/time.h>
#include <linux/list.h>
#include <asm/uaccess.h>

#include <linux/usb/composite.h>

struct ExtraDescriptor
{
    unsigned char                 * desc;
    struct list_head                list;
};

struct EndpointDescriptor
{
    struct usb_endpoint_descriptor  desc;
    struct ExtraDescriptor          extras;
    struct list_head                list;
};

struct InterfaceDescriptor
{
    struct usb_interface_descriptor desc;
    struct ExtraDescriptor          extras;
    struct EndpointDescriptor       endpoints;
    struct list_head                list;
};

struct ConfigDescriptor
{
    struct usb_config_descriptor    desc;
    struct InterfaceDescriptor      interfaces;
    struct ExtraDescriptor          extras;
    struct list_head                list;
};

struct DeviceDescriptor
{
    struct usb_device_descriptor    desc;
    struct ConfigDescriptor         configs;
};


struct DeviceDescriptor descriptor =
{
    .configs =
    {
        .list = LIST_HEAD_INIT(descriptor.configs.list),
        .extras =
        {
            .list = LIST_HEAD_INIT(descriptor.configs.extras.list),
        },
        .interfaces =
        {
            .list = LIST_HEAD_INIT(descriptor.configs.interfaces.list),
            .extras =
            {
                .list = LIST_HEAD_INIT(descriptor.configs.interfaces.extras.list),
            },
            .endpoints =
            {
                .list = LIST_HEAD_INIT(descriptor.configs.interfaces.endpoints.list),
                .extras =
                {
                    .list = LIST_HEAD_INIT(descriptor.configs.interfaces.endpoints.extras.list),
                },
            }
        }
    }
};

//INIT_LIST_HEAD(&descriptor.configs.list);



struct usb_endpoint_descriptor ep_desc_in =
{
    .bLength          = 7,
    .bDescriptorType  = 5,
    .bEndpointAddress = 0x82,
    .bmAttributes     = 2,
    .wMaxPacketSize   = cpu_to_le16(0x0200),
    .bInterval        = 1
};

struct usb_endpoint_descriptor ep_desc_out =
{
    .bLength          = 7,
    .bDescriptorType  = 5,
    .bEndpointAddress = 0x01,
    .bmAttributes     = 2,
    .wMaxPacketSize   = cpu_to_le16(0x0200),
    .bInterval        = 1
};


struct usb_endpoint_descriptor ep_desc_intr =
{
    .bLength          = 7,
    .bDescriptorType  = 5,
    .bEndpointAddress = 0x83,
    .bmAttributes     = 3,
    .wMaxPacketSize   = 0x0040,
    .bInterval        = 8
};


struct usb_request * requestEP0   = NULL;
struct usb_request * requestEPIN  = NULL;
struct usb_request * requestEPOUT = NULL;
struct usb_ep      * ep0          = NULL;
struct usb_ep      * epIn         = NULL;
struct usb_ep      * epOut        = NULL;
struct usb_gadget  * ggadget      = NULL;

struct Endpoint
{
    struct usb_endpoint_descriptor * desc;
    struct usb_ep                  * ep;
    struct usb_request             * req;
    struct list_head                 list;
};

struct Endpoint endpoints =
{
    .list = LIST_HEAD_INIT(endpoints.list)
};

spinlock_t lock = __SPIN_LOCK_UNLOCKED(lock);

char buffer[512*256];
size_t bufferlen = 0;

int  g_altfs_gadget_bind(struct usb_gadget * gadget);
void g_altfs_gadget_unbind(struct usb_gadget * gadget);
int  g_altfs_gadget_setup(struct usb_gadget * gadget, const struct usb_ctrlrequest * request);
void g_altfs_gadget_setup_complete(struct usb_ep *ep,struct usb_request *req);
void g_altfs_gadget_disconnect(struct usb_gadget * gadget);
void g_altfs_gadget_suspend(struct usb_gadget * gadget);
void g_altfs_gadget_resume(struct usb_gadget * gadget);

struct usb_gadget_driver driver =
{
    .function   = "g_altfs",
    .speed      = USB_SPEED_HIGH,
    .unbind     = g_altfs_gadget_unbind,
    .setup      = g_altfs_gadget_setup,
    .disconnect = g_altfs_gadget_disconnect,
    .suspend    = g_altfs_gadget_suspend,
    .resume     = g_altfs_gadget_resume,
    .driver	=
    {
        .owner  = THIS_MODULE,
        .name   = "g_altfs"
    }
};


void epIn_request_complete(struct usb_ep *ep,struct usb_request *req)
{
     printk(KERN_INFO "IN status %i actual %i\n", req->status, req->actual);

  req->status = 0;
}


void epOut_request_complete(struct usb_ep *uep,struct usb_request *req)
{
    char ep = 0x01;
    size_t s;

    if (req->status != 0)
    {

     printk(KERN_INFO "OUT status %i\n", req->status);

        usb_ep_queue(epOut, requestEPOUT, GFP_ATOMIC);

        return;
    }

    spin_lock(&lock);
    
    s = sizeof(ep) + req->actual;

    memcpy(buffer + bufferlen, &s, sizeof(s));
    memcpy(buffer + bufferlen + sizeof(s), &ep, 1);
    memcpy(buffer + bufferlen + sizeof(s) + sizeof(ep), req->buf, req->actual);
    bufferlen += (s + sizeof(s));

    spin_unlock(&lock);
    printk(KERN_INFO "OUT!\n");

    usb_ep_queue(uep, req, GFP_ATOMIC);
}


void g_altfs_gadget_request_complete(struct usb_ep * ep, struct usb_request * req)
{
    //printk(KERN_INFO "IN status %i actual %i\n", req->status, req->actual);

    if ((ep->address & USB_DIR_IN) == 0)
    {
        size_t s;

        spin_lock(&lock);
    
        s = sizeof(ep->address) + req->actual;

        memcpy(buffer + bufferlen, &s, sizeof(s));
        memcpy(buffer + bufferlen + sizeof(s), &ep->address, 1);
        memcpy(buffer + bufferlen + sizeof(s) + sizeof(ep->address), req->buf, req->actual);
        bufferlen += (s + sizeof(s));

        spin_unlock(&lock);
        //printk(KERN_INFO "OUT!\n");

        usb_ep_queue(ep, req, GFP_ATOMIC);
    }
    else
    {
        req->status = EAGAIN;
    }
}


struct usb_ep * g_altfs_gadget_configure_ep(struct usb_gadget * gadget, struct usb_endpoint_descriptor * epDesc)
{
    struct usb_ep * ep;

     list_for_each_entry (ep, &gadget->ep_list, ep_list)
     {
         if (ep->address == epDesc->bEndpointAddress)
         {
             return ep;
         }
     }

     return NULL;
}


int g_altfs_gadget_bind(struct usb_gadget * gadget)
{
    printk(KERN_INFO "bind!\n");
    if (gadget->is_otg) printk(KERN_INFO "otg\n");

    ggadget = gadget;
    ep0 = gadget->ep0;
    ep0->driver_data = NULL;

    requestEP0 = usb_ep_alloc_request(gadget->ep0, GFP_ATOMIC);

    if (!requestEP0)
    {
        return -ENOMEM;
    }

    requestEP0->buf = kmalloc(1024, GFP_ATOMIC);

    if (!requestEP0->buf)
    {
        usb_ep_free_request(gadget->ep0, requestEP0);

        return -ENOMEM;
    }

    requestEP0->context = kmalloc(8, GFP_ATOMIC);

    if (!requestEP0->context)
    {
        kfree(requestEP0->buf);
        usb_ep_free_request(gadget->ep0, requestEP0);

        return -ENOMEM;
    }

    requestEP0->zero = 1;
    requestEP0->length = 1024;
    requestEP0->status = EAGAIN;
    requestEP0->complete = g_altfs_gadget_setup_complete;

    // ****************************************************

    epIn = usb_ep_autoconfig(gadget, &ep_desc_in);

    if (!epIn)
    {
        printk(KERN_INFO "autoconf failed (in)\n");

        return -ENODEV;
    }

    requestEPIN = usb_ep_alloc_request(epIn, GFP_ATOMIC);

    if (!requestEPIN)
    {
        return -ENOMEM;
    }

    requestEPIN->buf = kmalloc(512, GFP_ATOMIC);

    if (!requestEPIN->buf)
    {
        usb_ep_free_request(epIn, requestEPIN);

        return -ENOMEM;
    }

    requestEPIN->zero = 1;
    requestEPIN->complete = epIn_request_complete;
    requestEPIN->length = 512;
    requestEPIN->status = 0;

    // ****************************************************

    epOut = usb_ep_autoconfig(gadget, &ep_desc_out);

    if (!epOut)
    {
        printk(KERN_INFO "autoconf failed (out)\n");

        return -ENODEV;
    }

    requestEPOUT = usb_ep_alloc_request(epOut, GFP_ATOMIC);

    if (!requestEPOUT)
    {
        return -ENOMEM;
    }

    requestEPOUT->buf = kmalloc(512, GFP_ATOMIC);

    if (!requestEPOUT->buf)
    {

        kfree(requestEPIN->buf);

        usb_ep_free_request(epIn, requestEPIN);
        usb_ep_free_request(epOut, requestEPOUT);

        return -ENOMEM;
    }

    requestEPOUT->complete = epOut_request_complete;
    requestEPOUT->length = 512;
    requestEPOUT->status = 0;

    //usb_ep_autoconfig_reset(gadget);

    //INIT_LIST_HEAD(&descriptor.configs.list);


    return 0;
}


void g_altfs_gadget_unbind(struct usb_gadget * gadget)
{
    printk(KERN_INFO "unbind\n");
    
    kfree(requestEPIN->buf);
    kfree(requestEPOUT->buf);
    kfree(requestEP0->context);
    kfree(requestEP0->buf);

    usb_ep_free_request(epOut, requestEPOUT);
    usb_ep_free_request(epIn, requestEPIN);
    usb_ep_free_request(gadget->ep0, requestEP0);


    struct list_head * posC, * posI, * posE, * q, * r, * s;
    struct ConfigDescriptor * config;
    struct InterfaceDescriptor * interface;
    struct EndpointDescriptor * endpoint;

    list_for_each_safe(posC, q, &descriptor.configs.list)
    {
        config = list_entry(posC, struct ConfigDescriptor, list);

        list_for_each_safe(posI, r, &config->interfaces.list)
        {
            interface = list_entry(posI, struct InterfaceDescriptor, list);

            list_for_each_safe(posE, s, &interface->endpoints.list)
            {
                endpoint = list_entry(posE, struct EndpointDescriptor, list);

                list_del(posE);
                kfree(endpoint);
            }

            list_del(posI);
            kfree(interface);
        }

        list_del(posC);
        kfree(config);
    }
}


void g_altfs_gadget_setup_complete(struct usb_ep * ep, struct usb_request * req)
{
    //printk(KERN_INFO "setup complete\n");

    struct usb_ctrlrequest * ctrlRequest = (struct usb_ctrlrequest *) req->context;

    unsigned char type  = ctrlRequest->wValue >> 0x08;
    unsigned char index = ctrlRequest->wValue &  0xFF;

    // Get configuration descriptor
    if ((ctrlRequest->bRequestType == USB_DIR_IN) && (ctrlRequest->bRequest == USB_REQ_GET_DESCRIPTOR) && (type == USB_DT_CONFIG) && (ctrlRequest->wIndex == 0)  && (ctrlRequest->wLength) >= sizeof(struct usb_config_descriptor))
    {
        struct list_head * pos;
        struct ConfigDescriptor * config = NULL;
        struct InterfaceDescriptor * interface = NULL;
        struct EndpointDescriptor * endpoint = NULL;
        unsigned char * buf = req->buf;

        do
        {
            if (buf[1] == USB_DT_CONFIG)
            {
                struct usb_config_descriptor * confDesc = (struct usb_config_descriptor *) buf;

                config = NULL;

                list_for_each(pos, &descriptor.configs.list)
                {
                    config = list_entry(pos, struct ConfigDescriptor, list);

                    if (config->desc.bConfigurationValue == confDesc->bConfigurationValue)
                    {
                        break;
                    }

                    config = NULL;
                }

                if (!config)
                {
                    config = kmalloc(sizeof(struct ConfigDescriptor), GFP_ATOMIC);

                    INIT_LIST_HEAD(&config->interfaces.list);

                    memcpy(&config->desc, buf, buf[0]);

                    list_add(&config->list, &descriptor.configs.list);
                }
            }

            if (buf[1] == USB_DT_INTERFACE)
            {
                struct usb_interface_descriptor * intfDesc = (struct usb_interface_descriptor *) buf;

                interface = NULL;

                list_for_each(pos, &config->interfaces.list)
                {
                    interface = list_entry(pos, struct InterfaceDescriptor, list);

                    if ((interface->desc.bInterfaceNumber == intfDesc->bInterfaceNumber) && (interface->desc.bAlternateSetting == intfDesc->bAlternateSetting))
                    {
                        break;
                    }

                    interface = NULL;
                }

                if (!interface)
                {
                    interface = kmalloc(sizeof(struct InterfaceDescriptor), GFP_ATOMIC);

                    INIT_LIST_HEAD(&interface->endpoints.list);

                    memcpy(&interface->desc, buf, buf[0]);

                    list_add(&interface->list, &config->interfaces.list);
                }
            }

            if (buf[1] == USB_DT_ENDPOINT)
            {
                struct usb_endpoint_descriptor * epDesc = (struct usb_endpoint_descriptor *) buf;

                endpoint = NULL;

                list_for_each(pos, &interface->endpoints.list)
                {
                    endpoint = list_entry(pos, struct EndpointDescriptor, list);

                    if (endpoint->desc.bEndpointAddress == epDesc->bEndpointAddress)
                    {
                        break;
                    }

                    endpoint = NULL;
                }

                if (!endpoint)
                {
                    endpoint = kmalloc(sizeof(struct EndpointDescriptor), GFP_ATOMIC);

                    memcpy(&endpoint->desc, buf, buf[0]);

                    list_add(&endpoint->list, &interface->endpoints.list);
                }
            }

            buf += buf[0];
        }
        while (buf < (((unsigned char *) req->buf) + ctrlRequest->wLength));
    }


    // Set configuration
    if ((ctrlRequest->bRequestType == USB_DIR_OUT) && (ctrlRequest->bRequest == USB_REQ_SET_CONFIGURATION) && (ctrlRequest->wIndex == 0)  && (ctrlRequest->wLength) == 0)
    {
        struct list_head * posC, * posI, * posE;
        struct ConfigDescriptor * config = NULL;
        struct InterfaceDescriptor * interface = NULL;
        struct EndpointDescriptor * endpoint = NULL;

        list_for_each(posC, &descriptor.configs.list)
        {
            config = list_entry(posC, struct ConfigDescriptor, list);

            if (config->desc.bConfigurationValue == index)
            {
                list_for_each(posI, &config->interfaces.list)
                {
                    interface = list_entry(posI, struct InterfaceDescriptor, list);

                    if (interface->desc.bAlternateSetting == 0)
                    {
                        list_for_each(posE, &interface->endpoints.list)
                        {
                            struct Endpoint * ep;

                            endpoint = list_entry(posE, struct EndpointDescriptor, list);

                            ep = kmalloc(sizeof(struct Endpoint), GFP_ATOMIC);

//                            ep->ep = usb_ep_autoconfig(ggadget, &endpoint->desc);
                            ep->ep = g_altfs_gadget_configure_ep(ggadget, &endpoint->desc);
                            ep->desc = &endpoint->desc;
                            ep->ep->desc = ep->desc;
                            ep->req = usb_ep_alloc_request(ep->ep, GFP_ATOMIC);
                            ep->req->buf = kmalloc(512*256, GFP_ATOMIC);

                            ep->req->zero = 1;
                            ep->req->complete = g_altfs_gadget_request_complete;
                            ep->req->length = 512*256;
                            ep->req->status = EAGAIN;

                            list_add(&ep->list, &endpoints.list);

                            usb_ep_enable(ep->ep);

                            if ((endpoint->desc.bEndpointAddress & USB_DIR_IN) == 0)
                            {
                                usb_ep_queue(ep->ep, ep->req, GFP_ATOMIC);
                            }
                        }
                    }
                }
            }
        }
    }

    req->status = EAGAIN;
}


int g_altfs_gadget_setup(struct usb_gadget * gadget, const struct usb_ctrlrequest * ctrlRequest)
{
    int result = -EOPNOTSUPP;
    size_t s;
    char ep = 0;

    printk(KERN_INFO "setup: bRequestType=%x  bRequest=%x wValue=%x wIndex=%x wLength=%x\n", ctrlRequest->bRequestType, ctrlRequest->bRequest, ctrlRequest->wValue, ctrlRequest->wIndex, ctrlRequest->wLength);

    spin_lock(&lock);

    s = sizeof(ep) + sizeof(struct usb_ctrlrequest);

    memcpy(buffer + bufferlen, &s, sizeof(s));
    memcpy(buffer + bufferlen + sizeof(s), &ep, 1);
    memcpy(buffer + bufferlen + sizeof(s) + sizeof(ep), ctrlRequest, sizeof(struct usb_ctrlrequest));
    bufferlen += (s + sizeof(s));

    result = USB_GADGET_DELAYED_STATUS;

    spin_unlock(&lock);

    memcpy(requestEP0->context, ctrlRequest, 8);

    // TODO    
    if ((ctrlRequest->bRequestType == 0) && (ctrlRequest->bRequest == 9) && (ctrlRequest->wValue == 1) && (ctrlRequest->wIndex == 0)  && (ctrlRequest->wLength) == 0)
    {
        result = 0;
    }

    return result;
}


void g_altfs_gadget_disconnect(struct usb_gadget * gadget)
{
    printk(KERN_INFO "disconnect\n");

    struct list_head * pos, * q;
    struct Endpoint  * endpoint;

    list_for_each_safe(pos, q, &endpoints.list)
    {
        endpoint = list_entry(pos, struct Endpoint, list);

        usb_ep_dequeue(endpoint->ep, endpoint->req);

        usb_ep_disable(endpoint->ep);
        kfree(endpoint->req->buf);
        usb_ep_free_request(endpoint->ep, endpoint->req);

        list_del(pos);
        kfree(endpoint);
    }
}


void g_altfs_gadget_suspend(struct usb_gadget * gadget)
{
    printk(KERN_INFO "suspend\n");
}


void g_altfs_gadget_resume(struct usb_gadget * gadget)
{
    printk(KERN_INFO "resume\n");
}


ssize_t g_altfs_gadget_read_buf(void * buf, size_t len)
{
    size_t result;

    if (len <= 0)
    {
        return len;
    }

    spin_lock(&lock);

    if (bufferlen != 0)
    {
        result = ((size_t *) buffer)[0];

        memcpy(buf, &buffer[sizeof(result)], result);

        bufferlen -= (result + sizeof(result));

        if (bufferlen > 0)
        {
            memcpy(buffer, &buffer[sizeof(result) + result], bufferlen - (sizeof(result) + result));

            bufferlen -= (sizeof(result) + result);
        }
        else
        {
            bufferlen = 0;
        }
    }
    else
    {
        result = 0;
    }
    
    spin_unlock(&lock);

    return result;
}


ssize_t g_altfs_gadget_write_buf(const char * buf, size_t len)
{
    ssize_t result;
    struct usb_ep      * ep = NULL;
    struct usb_request * req = NULL;

    struct list_head * pos;
    struct Endpoint  * endpoint;

    if (len <= 0)
    {
        return len;
    }

    if (buf[0] == 0x00)
    {
        ep = ep0;
        req = requestEP0;
    }
    else
    {
        list_for_each(pos, &endpoints.list)
        {
            endpoint = list_entry(pos, struct Endpoint, list);

            if (endpoint->desc->bEndpointAddress == buf[0])
            {
                ep = endpoint->ep;
                req = endpoint->req;

                break;
            }
        }
    }

    if (!ep)
    {
        return -EPIPE;
    }

    if (req->status != EAGAIN)
    {
        return -EAGAIN;
    }
    
    len--;
    buf++;

    result = copy_from_user(req->buf, buf, len);

    if (result != 0)
    {
        return -EAGAIN;
    }

    req->length = len;
    if ((len % 512) == 0) req->zero = 0; else req->zero = 1;

    result = usb_ep_queue(ep, req, GFP_ATOMIC);
//printk(KERN_INFO "len: %i (%i)\n", len, result);

    if (result < 0)
    {
        len = result;
    }

    return len;    
}


long g_altfs_gadget_start(void)
{
    return usb_gadget_probe_driver(&driver, g_altfs_gadget_bind);
}


void g_altfs_gadget_stop(void)
{
    usb_gadget_unregister_driver(&driver);
}
