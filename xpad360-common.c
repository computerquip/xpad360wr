/*
	TODO:
	Headsets
	
	NOTES:
	I'm forgetting the idea that the controller is HID compliant. 
	While other drivers do use a filter driver for HID, we do not as it's inconvenient. 
	It doesn't make anything less painful, especially since Linux doesn't have a filter driver. 

	PROBLEMS:
	There's an issue concerning interfaces. It's not a reliable method
	on determining controller number. There's actually a few reliable ways
	to confuse the driver and mix up controller numbers. 

	There's also data that I don't know how to interpret yet. 
	I'm assuming the announce has data in it that's obfuscated. 
	In order to reverse engineer that data, I need to work with the 
	XInput driver and feed it various values to see what it gives me. 
	I can't do this at the moment, I don't know how to go about it. 
*/

/*
 *	This file contains shared code among xpad controllers. 
 *	It handles things that never change among any xbox 360 gamepad. 
 */
#include "xpad360-common.h"

MODULE_AUTHOR("Zachary Lund <admin@computerquip.com>");
MODULE_DESCRIPTION("Xbox 360 Wireless Adapter");
MODULE_LICENSE("GPL");

/* These are functions used but implemented in specific modules. */
/* They don't belong in a header as they shouldn't be used by any other module.*/
int xpad360_init(struct xpad360_controller *controller);
void xpad360_destroy(struct xpad360_controller *controller);

int xpad360wr_init(struct xpad360_controller *controller);
void xpad360wr_destroy(struct xpad360_controller *controller);

static struct usb_device_id xpad360_table[] = {
	{ USB_DEVICE_INTERFACE_PROTOCOL(0x045E, 0x0719, 129) },
	{ USB_DEVICE_INTERFACE_PROTOCOL(0x045E, 0x028e, 1) },
	{}
};

static int xpad360_controller_open(struct input_dev* dev)
{
	struct xpad360_controller *controller = input_get_drvdata(dev);
	struct device *device = &(controller->usbintf->dev);
	
	/* We're already inquiring packets so no need to do that again. */
	dev_dbg(device, "Opening controller.");

	return 0;
}

static void xpad360_controller_close(struct input_dev* dev)
{
	struct xpad360_controller *controller = input_get_drvdata(dev);
	struct device *device = &(controller->usbintf->dev);
	
	dev_dbg(device, "Closing controller.");
	/* We cannot stop inquiring packets as connection packets are sent from the same interface. */
	/* In the case of the wired controller, it sends events when an attachment is attached. */
}

static void xpad360_common_input_capabilities(struct input_dev *inputdev) 
{
	#define SET_BIT(type) __set_bit(type, inputdev->keybit);

	__set_bit(EV_KEY, inputdev->evbit); /* General device that has key presses. */
	SET_BIT(BTN_A);
	SET_BIT(BTN_B);
	SET_BIT(BTN_X);
	SET_BIT(BTN_Y);
	SET_BIT(BTN_START);
	SET_BIT(BTN_SELECT);
	SET_BIT(BTN_THUMBL);
	SET_BIT(BTN_THUMBR);
	SET_BIT(BTN_TL);
	SET_BIT(BTN_TR);
	SET_BIT(BTN_MODE);

#undef SET_BIT
#define SET_BIT(type) \
	__set_bit(type, inputdev->absbit);\
	input_set_abs_params(inputdev, type, -32768, 32767, 16, 128);

	/* Axis... */
	__set_bit(EV_ABS, inputdev->evbit);
	SET_BIT(ABS_X);
	SET_BIT(ABS_Y);
	SET_BIT(ABS_RX);
	SET_BIT(ABS_RY);

#undef SET_BIT
#define SET_BIT(type) \
	__set_bit(type, inputdev->absbit); \
	input_set_abs_params(inputdev, type, 0, 255, 0, 0);

	/* Triggers... */
	SET_BIT(ABS_Z);
	SET_BIT(ABS_RZ);

#undef SET_BIT
#define SET_BIT(type) __set_bit(type, inputdev->ffbit)

	/* Force Feedback */
	__set_bit(EV_FF, inputdev->evbit);
	SET_BIT(FF_RUMBLE);

#undef SET_BIT
}

void xpad360_common_init_input_dev(struct input_dev *inputdev, struct usb_interface *intf)
{
	struct xpad360_controller *controller = usb_get_intfdata(intf);
	struct device *device = &intf->dev;
	struct usb_device *usbdev = interface_to_usbdev(intf);

	xpad360_common_input_capabilities(inputdev);
	
	inputdev->name = usbdev->product;
	inputdev->phys = controller->path;
	inputdev->dev.parent = device;
	inputdev->open = xpad360_controller_open;
	inputdev->close = xpad360_controller_close;
	
	usb_to_input_id(usbdev, &inputdev->id);
	input_set_drvdata(inputdev, controller); /* Why not? */
}

void xpad360_common_complete(struct urb *urb)
{
	struct xpad360_controller *controller = urb->context;
	struct device *device = &(controller->usbintf->dev);

	CHECK_URB_STATUS(device, urb)
}

/* 
 * This function is similar for all 360 controllers, only with different offsets. 
 * Anything uncommon is dealt with in specific modules. Calls input_sync.
 */
void xpad360_common_parse_input(struct input_dev *inputdev, void *_data){
	u8 *data = _data;

	/* start/back buttons */
	input_report_key(inputdev, BTN_START,  data[0] & 0x10);
	input_report_key(inputdev, BTN_SELECT, data[0] & 0x20); /* Back */

	/* stick press left/right */
	input_report_key(inputdev, BTN_THUMBL, data[0] & 0x40);
	input_report_key(inputdev, BTN_THUMBR, data[0] & 0x80);

	input_report_key(inputdev, BTN_TL,	data[1] & 0x01); /* Left Shoulder */
	input_report_key(inputdev, BTN_TR,	data[1] & 0x02); /* Right Shoulder */
	input_report_key(inputdev, BTN_MODE,	data[1] & 0x04); /* Guide */
	/* data[8] & 0x08 is a dummy value */
	input_report_key(inputdev, BTN_A,	data[1] & 0x10);
	input_report_key(inputdev, BTN_B,	data[1] & 0x20);
	input_report_key(inputdev, BTN_X,	data[1] & 0x40);
	input_report_key(inputdev, BTN_Y,	data[1] & 0x80);

	input_report_abs(inputdev, ABS_Z, data[2]);
	input_report_abs(inputdev, ABS_RZ, data[3]);

	/* left stick */
	input_report_abs(inputdev, ABS_X, (s16)le16_to_cpup((__le16*)&data[4]));
	input_report_abs(inputdev, ABS_Y, ~(s16)le16_to_cpup((__le16*)&data[6]));

	/* right stick */
	input_report_abs(inputdev, ABS_RX, (s16)le16_to_cpup((__le16*)&data[8]));
	input_report_abs(inputdev, ABS_RY, ~(s16)le16_to_cpup((__le16*)&data[10]));
	
	input_sync(inputdev);
}

/* Can be used on the same struct as long as the previous was cleaned or a reference is kept.  */
int xpad360_common_init_request(
	struct xpad360_request *request, 
	struct usb_interface *intf, 
	int direction, 
	void(*callback)(struct urb*),
	gfp_t mem_flags)
{
	struct usb_device * usbdev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *ep = &(intf->cur_altsetting->endpoint[direction].desc);
	void *data = usb_get_intfdata(intf);
	int error = 0;
	
	/* Allocate URB buffer */
	request->buffer =
		usb_alloc_coherent(
			usbdev,
			ep->wMaxPacketSize,
			mem_flags,
			&(request->dma)
		);
		
	if (unlikely(!request->buffer)) {
		error = -ENOMEM;
		return error;
	}
	
	/* Allocate URB */
	request->urb = usb_alloc_urb(0, mem_flags);
	
	if (unlikely(!request->urb)) {
		error = -ENOMEM;
		goto fail;
	}
	
	/* Fill URB struct */
	if (direction == XPAD360_EP_IN) {
		usb_fill_int_urb(
			request->urb, usbdev,
			usb_rcvintpipe(usbdev, ep->bEndpointAddress),
			request->buffer, ep->wMaxPacketSize, 
			callback, data, ep->bInterval
		);
	} else if (direction == XPAD360_EP_OUT) {
		usb_fill_int_urb(
			request->urb, usbdev,
			usb_sndintpipe(usbdev, ep->bEndpointAddress),
			request->buffer, ep->wMaxPacketSize,
			callback ? callback : xpad360_common_complete, 
			data, ep->bInterval
		);
	}
	
	request->urb->transfer_dma = request->dma;
	request->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	
	goto success;
fail:
	usb_free_coherent(
		usbdev,
		ep->wMaxPacketSize,
		request->buffer,
		request->dma
	);
	
success:
	return error;
}

void xpad360_common_destroy_request(
	struct xpad360_request *request, 
	struct usb_interface *intf,
	int direction)
{
	struct usb_device *usbdev = interface_to_usbdev(intf);
	
	usb_kill_urb(request->urb);
	usb_free_urb(request->urb);
	
	usb_free_coherent(
		usbdev,
		intf->cur_altsetting->endpoint[direction].desc.wMaxPacketSize,
		request->buffer,
		request->dma
	);
}

static int xpad360_common_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	//struct usb_device * usbdev = interface_to_usbdev(interface);
	struct xpad360_controller *controller = kzalloc(sizeof(struct xpad360_controller), GFP_KERNEL);
	struct device *device = &(interface->dev);
	int protocol = interface->cur_altsetting->desc.bInterfaceProtocol;
	int error = 0;
	
	if (unlikely(!controller)) {
		return -ENOMEM;
	}
	
	/* Make sure we have access to the controller and interface its on. */
	usb_set_intfdata(interface, controller);
	controller->usbintf = interface;
	
	/* Initialize common urbs */
	error = xpad360_common_init_request(
		&controller->out_led,
		interface,
		XPAD360_EP_OUT,
		NULL, GFP_KERNEL
	);
	
	if (unlikely(error)){
		dev_err(device, "controller->out_led failed to init!");
		goto fail0;
	}
	
	error = xpad360_common_init_request(
		&controller->out_rumble,
		interface,
		XPAD360_EP_OUT,
		NULL, GFP_KERNEL
	);
	
	if (unlikely(error)){
		dev_err(device, "controller->out_rumble failed to init!");
		goto fail1;
	}

	/* Branch code slightly based on wired and wireless. Based on bInterfaceProtocol. */
	/* This essentially initializes controller specific urbs and paths.  */
	
	/*
	 * The follow initialization functions *must* initialize controller->in. 
	 * It is also their job to clean up in the destroy functions. 
	 *
	 * Since controllers may differ, it's also their job to completely initialize 
	 * the input devices themselves. In the case of the wireless adapter, it's not
	 * even done in the init function.  
	 */
	switch (protocol) {
	case 129:
		error = xpad360wr_init(controller); break;
	case 1:
		error = xpad360_init(controller); break;
	default:
		break;
	}
	
	if (error) {
		goto fail2;
	}
	
	error = usb_submit_urb(controller->in->urb, GFP_KERNEL);
	if (unlikely(error)) {
		dev_err(device, "usb_submit_urb(controller->in.urb) failed!\n");
		goto fail3;
	}

	goto success;

fail3:
	switch (protocol) {
	case 129:
		xpad360wr_destroy(controller); break;
	case 1:
		xpad360_destroy(controller); break;
	default:
		break;
	}
fail2:
	xpad360_common_destroy_request(
		&controller->out_rumble,
		interface,
		XPAD360_EP_OUT
	);
fail1: 
	xpad360_common_destroy_request(
		&controller->out_led,
		interface,
		XPAD360_EP_OUT
	);
fail0:
	kfree(controller);
success:
	return error;
}

void xpad360_common_disconnect(struct usb_interface* interface)
{
	struct xpad360_controller *controller = usb_get_intfdata(interface);
	struct device *device = &(controller->usbintf->dev);
	int protocol = interface->cur_altsetting->desc.bInterfaceProtocol;
	
	dev_info(device, "Controller disconnected.\n");
		
	switch (protocol) {
	case 129:
		xpad360wr_destroy(controller); break;
	case 1:
		xpad360_destroy(controller); break;
	default:
		break;
	}
	
	xpad360_common_destroy_request(
		controller->in,
		interface,
		XPAD360_EP_IN
	);
	
	xpad360_common_destroy_request(
		&controller->out_led,
		interface,
		XPAD360_EP_OUT
	);
	
	xpad360_common_destroy_request(
		&controller->out_rumble,
		interface,
		XPAD360_EP_OUT
	);
	
	kfree(controller->in);
	kfree(controller);
}

static struct usb_driver xpad360_driver = {
	.name		= "xpad360",
	.probe		= xpad360_common_probe,
	.disconnect	= xpad360_common_disconnect,
	.id_table	= xpad360_table,
	.soft_unbind	= 1 /* Allows us to set LED properly before module unload. */
};

MODULE_DEVICE_TABLE(usb, xpad360_table);
module_usb_driver(xpad360_driver);