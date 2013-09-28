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

	struct urb *irq_in;
	struct urb *irq_out;
};

struct xpad360wr_headset {
	/* Nothing, I don't have one to test with. */
};

struct xpad360wr_adapter {
	struct xpad360wr_controller controllers[4]; /* Physical limitation of 4 controllers */
	struct xpad360wr_headset headsets[4];
};

/* TODO: Check for, and prevent, potential data races */
static struct xpad360wr_adapter g_Adapter;

static void xpad360wr_irq_receive(struct urb *urb){
	switch (urb->status){ 
		case 0: break;
		case -ECONNRESET:
		case -ENOENT:
		case -ESHUTDOWN:
			printk("Invalid status returned from %s\n", __FUNCTION__);
			return;
		default:
			printk("Unknown status returned from %s\n", __FUNCTION__);
			break;
	}

	printk("")

	usb_submit_urb(urb, GFP_ATOMIC);
}

int xpad360wr_probe(struct usb_interface *interface, const struct usb_device_id *id) {
	struct usb_device * usbdev = interface_to_usbdev(interface);
	struct usb_endpoint_descriptor *usbep = &(interface->cur_altsetting->endpoint[0].desc);

	struct xpad360wr_controller *controller;
	int error;

	{
		const u8 num_interface = interface->cur_altsetting->desc.bInterfaceNumber;
		/* Surely there's a way to simplify the above? */
		printk("Probing interface #%i\n", num_interface);

		/* All odd number interfaces are headsets which we don't handle. */
		if ((num_interface % 2) != 1)
			return -1;

		controller = &(g_Adapter.controllers[((num_interface + 1) / 2) - 1]);
		printk("Controller #%i Connected\n", (num_interface + 1) / 2);
	}

	/*
		Initialize input buffer for this controller
	 */
	controller->ep_in.buffer = 
	usb_alloc_coherent(
		usbdev, 
		MAX_PACKET_SIZE, 
		GFP_KERNEL, 
		&(controller->ep_in.dma)
	);

	if (!controller->ep_in.buffer){
		error = -ENOMEM;
		goto fail0;
	}

	controller->irq_in = usb_alloc_urb(0, GFP_KERNEL);

	if (!controller->irq_in) {
		error = -ENOMEM;
		goto fail1;
	}

/*
	if (!(controller->irq_out = usb_alloc_urb(0, GFP_KERNEL))) {
		error = -ENOMEM;
		got fail2;return
	}
*/

	usb_fill_int_urb(
		controller->irq_in, usbdev,
		usb_rcvintpipe(usbdev, usbep->bEndpointAddress),
		controller->ep_in.buffer, MAX_PACKET_SIZE, xpad360wr_irq_receive,
		NULL, usbep->bInterval /* Needs encoding which is why I don't just use 1 */
	);

	if (!usb_submit_urb(controller->irq_in, GFP_KERNEL)) {
		printk("usb_submit_urb(controller->irq_in) failed!");
		goto fail 2;
	}

	return 0;

	free2:
		usb_free_urb(controller->irq_in);
	fail1: 
		usb_free_coherent(
			usbdev,
			MAX_PACKET_SIZE,
			controller->ep_in.buffer,
			controller->ep_in.dma
		);

	fail0:
		return error;
;}

void xpad360wr_disconnect(struct usb_interface* interface) {
	struct usb_device * usbdev = interface_to_usbdev(interface);
	const u8 num_interface = interface->cur_altsetting->desc.bInterfaceNumber;
	struct xpad360wr_controller *controller = &(g_Adapter.controllers[((num_interface + 1) / 2) - 1]);

	usb_free_urb(controller->irq_in);

	usb_free_coherent(
		usbdev,
		MAX_PACKET_SIZE,
		g_Adapter.controllers[num_controller-1].ep_in.buffer,
		g_Adapter.controllers[num_controller-1].ep_in.dma
	);
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
