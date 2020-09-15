#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kref.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define OSRFX2_VENDOR 0x0547
#define OSRFX2_PRODUCT 0x1002

#define SUCCESS 0

MODULE_LICENSE("GPL");

static struct usb_device_id osrfx2_table [] = {
      { USB_DEVICE(OSRFX2_VENDOR, OSRFX2_PRODUCT) },
      { }
};

MODULE_DEVICE_TABLE(usb, osrfx2_table);

#define OSRFX2_MIN_BASE 192

struct osrfx2_skel{
    struct usb_device*      dev;
    struct usb_interface*   interface;
    unsigned char*          bulk_in_buffer;
    size_t                  bulk_in_buffer_size;
    u8                      bulk_in_endpointAddr;
    u8                      bulk_out_endpointAddr;
    struct kref             kref;
};

#define to_osrfx2_skel(d) container_of(d, struct osrfx2_skel, kref)

static struct usb_driver osrfx2_driver;

static void osrfx2_delete(struct kref* kref)
{
    struct osrfx2_skel* dev = NULL;

    dev = to_osrfx2_skel(kref);

    kfree(dev->bulk_in_buffer);
    kfree(dev);
}

static int osrfx2_open(struct inode * node, struct file * filep)
{
    int retval;
    int minor;
    struct usb_interface* interface;
    struct osrfx2_skel* dev = NULL;

    minor = iminor(node);

    interface = usb_find_interface(&osrfx2_driver, minor);

    if(!interface)
    {
        retval = -ENODEV;

        printk(KERN_ERR "osrfx2: Could not find USB interface\n");

        goto error;
    }

    dev = usb_get_intfdata(interface);

    if(!interface)
    {
        retval = -ENODEV;

        printk(KERN_ERR "osrfx2: Could not find USB device\n");

        goto error;
    }

    filep->private_data = dev;

    kref_get(&dev->kref);

    printk(KERN_INFO "osrfx2: Device opened\n");

    return SUCCESS;

error:

    return retval;
}

static int osrfx2_release (struct inode * node, struct file * filep)
{
    struct osrfx2_skel* dev = NULL;

    dev = (struct osrfx2_skel*) filep->private_data;

    kref_put(&dev->kref, osrfx2_delete);

    return SUCCESS;
}

static ssize_t osrfx2_read (struct file * filep, char __user * buffer, size_t count, loff_t * offset)
{
    int retval = 0;
    struct osrfx2_skel* dev;

    dev = (struct osrfx2_skel*)  filep->private_data;

    retval = usb_bulk_msg(dev->dev,
                 usb_rcvbulkpipe(dev->dev, dev->bulk_in_endpointAddr),
                 dev->bulk_in_buffer,
                 count < dev->bulk_in_buffer_size ? count : dev->bulk_in_buffer_size,
                 (int*) &count,
                 HZ);

    if(retval)
    {
        printk(KERN_ERR "osrfx2: Could not submit read bulk read message.");
    }
    else
    {
        if(copy_to_user(buffer, dev->bulk_in_buffer, count))
        {
            retval = -EFAULT;
        }
        else
        {
            retval = count;
        }
    }

    return retval;
}

static ssize_t osrfx2_write (struct file * filep, const char __user * buffer, size_t count, loff_t * offset)
{
    int retval = 0;
    struct osrfx2_skel* dev;

    dev = (struct osrfx2_skel*) filep->private_data;

    retval = usb_bulk_msg(
                dev->dev,
                usb_sndbulkpipe(dev->dev, dev->bulk_out_endpointAddr),
                buffer,
                count,
                (int*) &count,
                HZ);

    if(retval)
    {
        printk(KERN_ERR "osrfx2: Could not send bulk send message.");
    }

    return (ssize_t) count;
}


static struct file_operations osrfx2_fops = {
    .open = osrfx2_open,
    .write = osrfx2_write,
    .read = osrfx2_read,
    .release = osrfx2_release,
};

static char* osrfx2_devnode(struct device *dev, umode_t *mode)
{
    if(!mode)
        return NULL;

    *mode = 0666;

    return NULL;
}

static struct usb_class_driver osrfx2_class = {
    .name = "usb/osrfx2%d",
    .devnode = osrfx2_devnode,
    .fops = &osrfx2_fops,
    .minor_base = OSRFX2_MIN_BASE,
};

static int osrfx2_probe(struct usb_interface* intf, const struct usb_device_id* id)
{
    int ii;
    struct usb_host_interface* iface_desc;
    struct usb_endpoint_descriptor* endpoint;

    struct osrfx2_skel* dev = NULL;

    int retval = 0;

    //Alloc dev structure

    dev = kzalloc(sizeof(struct osrfx2_skel), GFP_KERNEL);

    if(dev == NULL)
    {
        printk(KERN_ERR "osrfx2: Could not alloc osfrx2 struct\n");

        goto error;
    }

    dev->dev = usb_get_dev(interface_to_usbdev(intf));
    dev->interface = intf;

    kref_init(&dev->kref);

    usb_set_intfdata(intf, dev);

    //Loop endpoints

    iface_desc = intf->cur_altsetting;

    for(ii = 0; ii < iface_desc->desc.bNumEndpoints; ii++)
    {
        endpoint = &iface_desc->endpoint[ii].desc;

        if(endpoint->bEndpointAddress & USB_DIR_IN &&
                (endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK)
        {
            printk(KERN_INFO "osrfx2: In bulk endpoint found at enpoint %d\n", endpoint->bEndpointAddress);

            dev->bulk_in_buffer_size = endpoint->wMaxPacketSize;

            dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;

            dev->bulk_in_buffer = kzalloc(dev->bulk_in_buffer_size, GFP_KERNEL);
        }

        if(endpoint->bEndpointAddress & USB_DIR_OUT &&
                (endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK)
        {
            printk(KERN_INFO "osrfx2: Out bulk endpoint found at enpoint %d\n", endpoint->bEndpointAddress);
        }
    }

    //Register usb device

    retval = usb_register_dev(dev->interface, &osrfx2_class);

    if(retval)
    {
        printk(KERN_ERR "osrfx2: Could not register usb driver\n");
    }

    printk(KERN_INFO "osrfx2: Device was connected\n");

    return SUCCESS;

error:

    if(dev)
    {
        kref_put(&dev->kref, osrfx2_delete);
    }

    return retval;
}

static void osrfx2_disconnect(struct usb_interface* intf)
{
    struct osrfx2_skel* dev;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// GUARANTEE EXCLUSIVE ACCESS

    dev = usb_get_intfdata(intf);

    if(!dev)
        return;

    kref_put(&dev->kref, osrfx2_delete);
}

static struct usb_driver osrfx2_driver = {
  .name = "osrfx2",
  .id_table = osrfx2_table,
  .probe = osrfx2_probe,
  .disconnect = osrfx2_disconnect,
};

static int osrfx2_init(void)
{
    int result;

    result = usb_register(&osrfx2_driver);

    if(result)
        printk(KERN_ERR "osrfx2: Could not register osrfx2 driver. Error code: %d\n", result);

    printk(KERN_INFO "osrfx2: USB OSRFX2 successully registered\n");

    return SUCCESS;
}

static void osrfx2_exit(void)
{
    usb_deregister(&osrfx2_driver);

    printk(KERN_INFO "Uosrfx2: SB OSRFX2 successully deregistered\n");
}

module_init(osrfx2_init)
module_exit(osrfx2_exit)