#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kref.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define OSRFX2_VENDOR   0x0547
#define OSRFX2_PRODUCT  0x1002

#define OSRFX2_MIN_BASE 192

#define OSRFX2_TIMEOUT  HZ

#define SUCCESS         0

MODULE_LICENSE("GPL");

static struct usb_device_id osrfx2_table [] = {
      { USB_DEVICE(OSRFX2_VENDOR, OSRFX2_PRODUCT) },
      { }
};

MODULE_DEVICE_TABLE(usb, osrfx2_table);

struct switches {
    union {
        struct
        {
            char sw1 : 1;
            char sw2 : 1;
            char sw3 : 1;
            char sw4 : 1;
            char sw5 : 1;
            char sw6 : 1;
            char sw7 : 1;
            char sw8 : 1;
        };

        char octet;
    };
};


struct osrfx2_skel{

    struct usb_device*      dev;
    struct usb_interface*   interface;

    unsigned char*          bulk_in_buffer;
    size_t                  bulk_in_maxBufferSize;
    u16                     bulk_in_endpointAddr;

    u8                      bulk_out_endpointAddr;
    u16                     bulk_out_maxBufferSize;

    struct urb*             interrupt_urb;

    unsigned char*          interrupt_buffer;
    u16                     interrupt_maxPacketSize;
    u8                      interrupt_endpointAddr;
    u8                      interrupt_interval;

    struct switches         switches_state;

    struct kref             kref;
};

#define to_osrfx2_skel(d) container_of(d, struct osrfx2_skel, kref)

static struct usb_driver osrfx2_driver;

static void osrfx2_delete(struct kref* kref)
{
    struct osrfx2_skel* dev = NULL;

    dev = to_osrfx2_skel(kref);

    if(dev->bulk_in_buffer != NULL)
    {
        kfree(dev->bulk_in_buffer);
    }

    if(dev->interrupt_buffer != NULL)
    {
        kfree(dev->interrupt_buffer);
    }

    if(dev->interrupt_urb != NULL)
    {
        usb_free_urb(dev->interrupt_urb);
    }

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

    if(interface == NULL)
    {
        retval = -ENODEV;

        printk(KERN_ERR "osrfx2: Could not find USB interface\n");

        goto error;
    }

    dev = usb_get_intfdata(interface);

    if(dev == NULL)
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

    retval = usb_bulk_msg(
                dev->dev,
                usb_rcvbulkpipe(dev->dev, dev->bulk_in_endpointAddr),
                dev->bulk_in_buffer,
                count < dev->bulk_in_maxBufferSize ? count : dev->bulk_in_maxBufferSize,
                (int*) &count,
                OSRFX2_TIMEOUT);

    if(retval != 0)
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

static void osrfx2_write_callback(struct urb* urb)
{
    struct osrfx2_skel* dev;

    dev = urb->context;

    if(urb->status)
    {
        printk(KERN_ERR "osrfx2: Could not successfully transfer the data to the device");
    }
    else
    {
        printk(KERN_INFO "osrfx2: Data successfully transferred to the device");
    }

    usb_free_coherent(dev->dev, urb->transfer_buffer_length, urb->transfer_buffer, urb->transfer_dma);
}

static ssize_t osrfx2_write (struct file * filep, const char __user * buffer, size_t count, loff_t * offset)
{
    int retval = 0;
    struct osrfx2_skel* dev;

    char* buf = NULL;
    struct urb* urb = NULL;

    dev = (struct osrfx2_skel*) filep->private_data;

    if(count == 0)
        return count;

    urb = usb_alloc_urb(0, GFP_KERNEL);

    if(urb == NULL)
    {
        retval = -ENOMEM;
        printk(KERN_ERR "osrfx2: Could not alloc urb\n");

        goto error;
    }

    buf = usb_alloc_coherent(dev->dev, count, GFP_KERNEL, &urb->transfer_dma);

    if(buf == NULL)
    {
        retval = -ENOMEM;
        printk(KERN_ERR "osrfx2: Could not allocate buffer\n");

        goto error;
    }

    if(copy_from_user(buf, buffer, count))
    {
        retval = -EFAULT;

        goto error;
    }

    usb_fill_bulk_urb(
                urb,
                dev->dev,
                usb_sndbulkpipe(dev->dev, dev->bulk_out_endpointAddr),
                buf,
                count,
                osrfx2_write_callback,
                dev);

    retval = usb_submit_urb(urb, GFP_KERNEL);

    if(retval != 0)
    {
        printk(KERN_ERR "osrfx2: Could not send bulk send message\n");

        goto error;
    }

    usb_free_urb(urb);

    return count;

error:


    if(buf != NULL)
    {
        usb_free_coherent(dev->dev, count, buf, urb->transfer_dma);
        kfree(buf);
    }

    if(urb != NULL)
    {
        usb_free_urb(urb);
    }

    return retval;
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

static void interrupt_callback(struct urb* urb)
{
    int res = 0;
    struct osrfx2_skel* dev;

    struct switches* packet;

    dev = urb->context;

    if(urb->status == SUCCESS)
    {
        //UPDATE VALUE
        packet = urb->transfer_buffer;

        dev->switches_state = *packet;


        //SUBMIT URB INTERRUPT

        res = usb_submit_urb(dev->interrupt_urb, GFP_ATOMIC);

        if(res != 0)
        {
            dev_err(&dev->dev->dev, "osrfx2: Urb could not be submited. Error code: %04x", res);
        }

        return;
    }

    switch (urb->status) {
            case -ECONNRESET:
            case -ENOENT:
            case -ESHUTDOWN:
                return;
            default:
                dev_err(&dev->dev->dev, "osrfx2: Urb could not be submited. Error code: %04x", res);

                return;
    }
}

static int init_interrupt(struct osrfx2_skel* dev)
{
    int retval = 0;

    dev->interrupt_urb = usb_alloc_urb(0, GFP_KERNEL);

    if(dev->interrupt_urb == NULL)
    {
        retval = -ENOMEM;

        goto error;
    }

    dev->interrupt_buffer = kzalloc(dev->interrupt_maxPacketSize, GFP_KERNEL);

    if(dev->interrupt_urb == NULL)
    {
        retval = -ENOMEM;

        goto error;
    }

    usb_fill_int_urb(
                dev->interrupt_urb,
                dev->dev,
                usb_rcvintpipe(dev->dev, dev->interrupt_endpointAddr),
                dev->interrupt_buffer,
                dev->interrupt_maxPacketSize,
                interrupt_callback,
                dev,
                dev->interrupt_interval);

    retval = usb_submit_urb(dev->interrupt_urb, GFP_KERNEL);

    if(retval != 0)
    {
        goto error;
    }

    return retval;

error:

    if(dev->interrupt_buffer != NULL)
    {
        kfree(dev->interrupt_buffer);

        dev->interrupt_buffer = NULL;
    }

    if(dev->interrupt_urb != NULL)
    {
        usb_free_urb(dev->interrupt_urb);

        dev->interrupt_urb = NULL;
    }

    return retval;
}

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

            dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
            dev->bulk_in_maxBufferSize = endpoint->wMaxPacketSize;
            dev->bulk_in_buffer = kzalloc(dev->bulk_in_maxBufferSize, GFP_KERNEL);

            if(dev->bulk_in_buffer == NULL)
            {
                retval = -ENOMEM;

                goto error;
            }
        }

        if(endpoint->bEndpointAddress & USB_DIR_OUT &&
                (endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK)
        {
            printk(KERN_INFO "osrfx2: Out bulk endpoint found at enpoint %d\n", endpoint->bEndpointAddress);

            dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
            dev->bulk_out_maxBufferSize = endpoint->wMaxPacketSize;
        }

        if((endpoint->bEndpointAddress & USB_DIR_OUT) &&
                (endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT)
        {
            printk(KERN_INFO "osrfx2: Out interrupt found at endpoint %d\n", endpoint->bEndpointAddress);

            dev->interrupt_endpointAddr = endpoint->bEndpointAddress;
            dev->interrupt_interval = endpoint->bInterval;
            dev->interrupt_maxPacketSize = endpoint->wMaxPacketSize;

            init_interrupt(dev);
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

    if(dev != NULL)
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

    if(result != 0)
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
