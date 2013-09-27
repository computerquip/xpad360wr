#include <linux/module.h>
#include <linux/usb/input.h>

MODULE_AUTHOR("Zachary Lund <admin@computerquip.com>");
MODULE_DESCRIPTION("Xbox 360 Wireless Adapter");
MODULE_LICENSE("GPL");

/* These are constant values from the USB device that we already know. */
/* We shouldn't have to query the device for these things.*/
#define MAX_PACKET_SIZE 32

static struct usb_device_id xpad360wr_table[] = {
	{ USB_DEVICE(0x045e /* Microsoft */, 0x0719)},
	{}
};

MODULE_DEVICE_TABLE(usb, xpad360wr_table);

struct xpad360wr_buffer {
	dma_addr_t dma; /**/
	void * buffer;
};

struct xpad360wr_controller {
	struct usb_interface *usbdev;
	bool present;

	struct xpad360wr_buffer ep_in;
	struct xpad360wr_buffer ep_out;
};

struct xpad360wr_headset {
	/* Nothing, I don't have one to test with. */
};

struct xpad360wr_adapter {
	struct xpad360wr_controller controllers[4]; /* Physical limitation of 4 controllers */
	struct xpad360wr_headset headsets[4];
};

static struct xpad360wr_adapter g_Adapter;

int xpad360wr_probe(struct usb_interface *interface, const struct usb_device_id *id) {
	struct usb_device * usbdev = interface_to_usbdev(interface);
	u8 num_controller = 0;

	{
		const u8 num_interface = interface->cur_altsetting->desc.bInterfaceNumber;
		/* Surely there's a way to simplify the above? */
		printk("Probing interface #%i\n", num_interface);

		if ((num_interface % 2) != 1)
			return -2;

		num_controller = (num_interface + 1) / 2;

		if (num_controller == 0)
			return -3;
	}

	g_Adapter.controllers[num_controller].ep_in.buffer = 
	usb_alloc_coherent(
		usbdev, 
		MAX_PACKET_SIZE, 
		GFP_KERNEL, 
		&(g_Adapter.controllers[num_controller-1].ep_in.dma)
	);

	printk("Controller #%i Connected. - xpad360wr\n", num_controller);

	return 0
;}

void xpad360wr_disconnect(struct usb_interface* interface) {

}

static struct usb_driver xpad360wr_driver = {
	.name		= "xpad360wr",
	.probe		= xpad360wr_probe,
	.disconnect	= xpad360wr_disconnect,
	.id_table	= xpad360wr_table,
};


static int __init xpad360wr_init( void ) {
	int err = 0;

	err = usb_register(&xpad360wr_driver);
	if (err)
		return err;

	printk("xpad360wr in da house!\n");
	return err;
}

static void __exit xpad360wr_exit( void ) {
	usb_deregister(&xpad360wr_driver);
}

module_init(xpad360wr_init);
module_exit(xpad360wr_exit);
