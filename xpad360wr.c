#include <linux/module.h>
#include <linux/usb/input.h>

MODULE_AUTHOR("Zachary Lund <admin@computerquip.com>");
MODULE_DESCRIPTION("Xbox 360 Wireless Adapter");
MODULE_LICENSE("GPL");

/* These are constant values from the USB device that we already know. */
/* We shouldn't have to query the device for these things.*/
#define MAX_PACKET_SIZE 32

static struct usb_device_id xpad360wa_table[] = {
	{ USB_DEVICE(0x045e /* Microsoft */, 0x0719)},
	{}
};

MODULE_DEVICE_TABLE(usb, xpad360wa_table);

struct xpad360wa_buffer {
	dma_addr_t *dma; /**/
	void * buffer;
};

struct xpad360wa_controller {
	struct usb_interface *usbdev;
	bool present;

	struct xpad360wa_buffer ep_in;
	struct xpad360wa_buffer ep_out;
};

struct xpad360wa_headset {
	/* Nothing, I don't have one to test with. */
};

struct xpad360wa_adapter {
	struct xpad360wa_controller controllers[4]; /* Physical limitation of 4 controllers */
	struct xpad360wa_headset headsets[4];
};

static struct xpad360wa_adapter g_Adapter;

int xpad360wa_probe(struct usb_interface *interface, const struct usb_device_id *id) {
	struct usb_device * usbdev = interface_to_usbdev(interface);
	u8 num_controller = 0;

	{
		const u8 num_interface = interface->cur_altsetting->desc.bInterfaceNumber;
		/* Surely there's a way to simplify the above? */

		if ((num_interface % 2) != 1)
			return -1;

		num_controller = (num_interface + 1) / 2;

		if (num_controller == 0)
			return -1;
	}

	g_Adapter.controllers[num_controller].ep_in.buffer = 
	usb_alloc_coherent(
		usbdev, 
		MAX_PACKET_SIZE, 
		GFP_KERNEL, 
		g_Adapter.controllers[num_controller].ep_in.dma
	);

	printk("Controller #%i Connected. - xpad360wa", num_controller);

	return 0;
}

void xpad360wa_disconnect(struct usb_interface* interface) {

}

static struct usb_driver xpad360wa_driver = {
	.name		= "xpad360wa",
	.probe		= xpad360wa_probe,
	.disconnect	= xpad360wa_disconnect,
	.id_table	= xpad360wa_table,
};


static int __init xpad360wa_init( void ) {
	int err = 0;

	err = usb_register(&xpad360wa_driver);
	if (err)
		return err;

	printk("xpad360wa in da house!");
	return err;
}

static void __exit xpad360wa_exit( void ) {
	usb_deregister(&xpad360wa_driver);
}

module_init(xpad360wa_init);
module_exit(xpad360wa_exit);
