#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb/input.h>

MODULE_AUTHOR("Zachary Lund <admin@computerquip.com>");
MODULE_DESCRIPTION("Xbox 360 Wireless Adapter");
MODULE_LICENSE("GPL");

/* These are constant values from the USB device that we already know. */
/* We shouldn't have to query the device for these things.*/
#define MAX_PACKET_SIZE 32

struct xpad360wr_buffer {
	dma_addr_t dma; /**/
	void *buffer;
};

struct xpad360wr_headset {
	/* Nothing, I don't have one to test with. */
};

struct xpad360wr_controller {
	struct input_dev *inputdev; /* input subsystem device */
	struct usb_interface *usbdev; /* usb subsystem device */
	bool present;

	struct xpad360wr_buffer ep_in;
	struct xpad360wr_buffer ep_out;

	struct xpad360wr_headset *headset; /* Currently nothing since I don't have a headset */

	struct urb *irq_in;
	struct urb *irq_out;

	char path[64]; /* Physical stable path we can reference to */
};

static int xpad360wr_controller_open(struct input_dev* dev) {
	//struct xpad360wr_controller *controller = input_get_drvdata(dev);

	/* Herm... */
	printk("xpad360wr_controller_open() called!");

	return 0;
}

static void xpad360wr_controller_close(struct input_dev* dev) {
	//struct xpad360wr_controller *controller = input_get_drvdata(dev);
	printk("xpad360wr_controller_closed() called!");
}

static void xpad360wr_irq_send(struct urb *urb){
	switch (urb->status) {
		case 0: return;
		case -ECONNRESET:
		case -ENOENT:
		case -ESHUTDOWN:
			return;
		default:
			printk("Unknown status returned from %s\n", __FUNCTION__);
	}

	usb_submit_urb(urb, GFP_ATOMIC);
}

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

	printk("Stub for IRQ IN packets.");

	usb_submit_urb(urb, GFP_ATOMIC);
}

int xpad360wr_probe(struct usb_interface *interface, const struct usb_device_id *id) {
	struct usb_device * usbdev = interface_to_usbdev(interface);
	struct usb_endpoint_descriptor *usbep = &(interface->cur_altsetting->endpoint[0].desc);
	struct xpad360wr_controller *controller = kzalloc(sizeof(struct xpad360wr_controller), GFP_KERNEL);
	int error = 0;

	{
		const u8 num_interface = interface->cur_altsetting->desc.bInterfaceNumber;
		/* Surely there's a way to simplify the above? */
		printk("Probing interface #%i\n", num_interface);

		/* All even number interfaces are headsets which we don't handle. */
		if ((num_interface % 2) != 1)
			return -1;

		printk("Controller #%i Connected\n", (num_interface + 1) / 2);
	}

	/* 
		Allocate input subsystem device
	*/
	controller->inputdev = input_allocate_device();
	if (controller->inputdev == NULL) {
		error = -ENOMEM;
		goto fail0;
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

	/*
		Allocate URB (USB Request Block)
	 */
	controller->irq_in = usb_alloc_urb(0, GFP_KERNEL);

	if (!controller->irq_in) {
		error = -ENOMEM;
		goto fail1;
	}

	/* 
		Initialize input device with usb device
	 */
	usb_make_path(usbdev, controller->path, sizeof(controller->path));
	controller->inputdev->name = "Xbox 360 Wireless Receiver"; /* HARD CODED, OMG, FIX */
	controller->inputdev->phys = controller->path; /* Probably more issues here... */
	controller->inputdev->dev.parent = &(interface->dev);
	controller->inputdev->open = xpad360wr_controller_open;
	controller->inputdev->close = xpad360wr_controller_close;

	input_set_drvdata(controller->inputdev, controller);
	usb_set_intfdata(interface, controller);

	/*
		Initialize URB
	 */
	usb_fill_int_urb(
		controller->irq_in, usbdev,
		usb_rcvintpipe(usbdev, usbep->bEndpointAddress),
		controller->ep_in.buffer, MAX_PACKET_SIZE, xpad360wr_irq_receive,
		controller, usbep->bInterval /* Needs encoding which is why I don't just use 1 */
	);

	controller->irq_in->transfer_dma
		= controller->ep_in.dma;
	controller->irq_in->transfer_flags
		|= URB_NO_TRANSFER_DMA_MAP;

	/*
		Now we begin the same as above except for out.
	 */
	usbep = &(interface->cur_altsetting->endpoint[1].desc);

	controller->ep_out.buffer = 
	usb_alloc_coherent(
		usbdev, 
		MAX_PACKET_SIZE, 
		GFP_KERNEL, 
		&(controller->ep_out.dma)
	);

	if (!controller->ep_out.buffer) {
		error = -ENOMEM;
		goto fail2;
	}

	controller->irq_out = usb_alloc_urb(0, GFP_KERNEL);

	if (!controller->irq_out) {
		error = -ENOMEM;
		goto fail3;
	}

	usb_fill_int_urb(
		controller->irq_out, usbdev,
		usb_sndintpipe(usbdev, usbep->bEndpointAddress),
		controller->ep_out.buffer, MAX_PACKET_SIZE, xpad360wr_irq_send,
		controller, usbep->bInterval
	);

	/*
		Controller immediately sends us stuff. 
		I cannot figure out what xpad driver is doing... 
	 */
	/*
		Submit URB. This should complete and call our callback (xpad360wr_irq_recieve)
	 */
	controller->irq_in->dev = usbdev;
	error = usb_submit_urb(controller->irq_in, GFP_KERNEL);
	if (error != 0) {
		printk("usb_submit_urb(controller->irq_in) failed!\n");
		goto fail4;
	}

	return 0;
	fail4:
		usb_free_urb(controller->irq_out);
	fail3:
		usb_free_coherent(
			usbdev,
			MAX_PACKET_SIZE,
			controller->ep_out.buffer,
			controller->ep_out.dma
		);
	fail2:
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
	struct xpad360wr_controller *controller = usb_get_intfdata(interface);

	input_unregister_device(controller->inputdev);

	usb_free_coherent(
		usbdev,
		MAX_PACKET_SIZE,
		controller->ep_in.buffer,
		controller->ep_in.dma
	);

	usb_free_coherent(
		usbdev,
		MAX_PACKET_SIZE,
		controller->ep_out.buffer,
		controller->ep_out.dma
	);

	usb_free_urb(controller->irq_in);
	usb_free_urb(controller->irq_out);

	kfree(controller);
	usb_set_intfdata(interface, NULL);
}

static struct usb_device_id xpad360wr_table[] = {
	{ USB_DEVICE(0x045e /* Microsoft */, 0x0719)},
	{}
};


static struct usb_driver xpad360wr_driver = {
	.name		= "xpad360wr",
	.probe		= xpad360wr_probe,
	.disconnect	= xpad360wr_disconnect,
	.id_table	= xpad360wr_table,
};

MODULE_DEVICE_TABLE(usb, xpad360wr_table);
module_usb_driver(xpad360wr_driver);