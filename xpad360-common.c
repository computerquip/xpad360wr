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
void xpad360_receive(struct urb* urb);
void xpad360_set_led(struct xpad360_controller *controller, u8 status);
void xpad360_set_led_sync(struct xpad360_controller *controller, u8 status);
int xpad360_rumble(struct input_dev *dev, void* stuff, struct ff_effect *effect);

void xpad360wr_receive(struct urb* urb);
void xpad360wr_set_led_sync(struct xpad360_controller *controller, u8 status);
void xpad360wr_query_presence(struct xpad360_controller *controller);
int xpad360wr_rumble(struct input_dev *dev, void *stuff, struct ff_effect *effect);

static struct usb_device_id xpad360_table[] = {
	{ USB_DEVICE_INTERFACE_PROTOCOL(0x045E, 0x0719, 129) },
	{ USB_DEVICE_INTERFACE_PROTOCOL(0x045E, 0x028e, 1) },
	{}
};

void xpad360_common_register_input_work(struct work_struct* work)
{
	struct input_work *inputwork = (struct input_work*)work;

	input_register_device(inputwork->inputdev);
}

void xpad360_common_unregister_input_work(struct work_struct* work)
{
	struct input_work *inputwork = (struct input_work*)work;

	input_unregister_device(inputwork->inputdev);
}

static int xpad360_controller_open(struct input_dev* dev)
{
	struct xpad360_controller *controller = input_get_drvdata(dev);
	struct device *device = &(controller->usbintf->dev);

	/* We're already inquiring packets so no need to do that again. */
	dev_dbg(device, "Opening controller...");

	if (controller->present == false) {
		dev_dbg(device, "failed.\n");
		return -ENODEV; /* Is this appropriate? */
	}

	dev_dbg(device, "success.\n");

	return 0;
}

static void xpad360_controller_close(struct input_dev* dev)
{
	struct xpad360_controller *controller = input_get_drvdata(dev);
	struct device *device = &(controller->usbintf->dev);

	dev_dbg(device, "Closing controller.");
	/* We cannot stop inquiring packets as connection packets are sent from the same interface. */
}

static void xpad360_common_complete(struct urb *urb)
{
	struct xpad360_controller *controller = urb->context;
	struct device *device = &(controller->usbintf->dev);

	CHECK_URB_STATUS(urb)
}

/* 
 * This function is similar for all 360 controllers, only with different offsets. 
 * Anything uncommon is dealt with in specific modules. Calls input_sync.
 */
void xpad360_common_parse_input(struct xpad360_controller *controller, void *_data){
	struct input_dev *inputdev = controller->inputdev;
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

static int xpad360_common_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device * usbdev = interface_to_usbdev(interface);
	struct usb_endpoint_descriptor *ep_in = &(interface->cur_altsetting->endpoint[0].desc);
	struct usb_endpoint_descriptor *ep_out = &(interface->cur_altsetting->endpoint[1].desc);
	struct xpad360_controller *controller = kzalloc(sizeof(struct xpad360_controller), GFP_KERNEL);
	struct device *device = &(interface->dev);
	int protocol = interface->cur_altsetting->desc.bInterfaceProtocol;
	int error = 0;
	
	dev_dbg(device, "Device: %s\nSerial: %s\n", usbdev->product, usbdev->serial);
	
	if (!controller) {
		return -ENOMEM;
	}
	
	controller->usbintf = interface;
	
	/* Allocate input structure */
	controller->inputdev = devm_input_allocate_device(device);
	
	if (unlikely(controller->inputdev == NULL)) {
		error = -ENOMEM;
		goto fail0;
	}
	
	controller->register_input.inputdev = controller->inputdev;
	controller->unregister_input.inputdev = controller->inputdev;

	INIT_WORK((struct work_struct *)&controller->register_input, xpad360_common_register_input_work);
	INIT_WORK((struct work_struct *)&controller->unregister_input, xpad360_common_unregister_input_work);

	/* Allocate in and out buffers*/
	controller->in.buffer =
		usb_alloc_coherent(
			usbdev,
			ep_in->wMaxPacketSize,
			GFP_KERNEL,
			&(controller->in.dma)
		);
		
	
	if (unlikely(!controller->in.buffer)) {
		error = -ENOMEM;
		goto fail0;
	}
		
	controller->out.buffer =
		usb_alloc_coherent(
			usbdev,
			ep_out->wMaxPacketSize,
			GFP_KERNEL,
			&(controller->out.dma)
		);

	if (unlikely(!controller->out.buffer)) {
		error = -ENOMEM;
		goto fail1;
	}

	/* Allocate in and out URBs */
	controller->in.urb = usb_alloc_urb(0, GFP_KERNEL);

	if (unlikely(!controller->in.urb)) {
		error = -ENOMEM;
		goto fail2;
	}
	
	controller->out.urb = usb_alloc_urb(0, GFP_KERNEL);
	
	if (unlikely(!controller->out.urb)) {
		error = -ENOMEM;
		goto fail3;
	}

	/* Initialize input device */
	controller->inputdev->name = usbdev->product;
	controller->inputdev->phys = controller->path;
	controller->inputdev->dev.parent = device;
	controller->inputdev->open = xpad360_controller_open;
	controller->inputdev->close = xpad360_controller_close;

	usb_to_input_id(usbdev, &controller->inputdev->id);


	controller->num_controller = (interface->cur_altsetting->desc.bInterfaceNumber + 1) / 2;
	
	{
		char tmp[8];
		snprintf(tmp, sizeof(tmp), "/input%.1i", controller->num_controller);
		usb_make_path(usbdev, controller->path, sizeof(controller->path));
		strlcat(controller->path, tmp, sizeof(controller->path));
	}
	
	input_set_drvdata(controller->inputdev, controller);
	usb_set_intfdata(interface, controller);

	/* Initialize URBs*/
	usb_fill_int_urb(
		controller->in.urb, usbdev,
		usb_rcvintpipe(usbdev, ep_in->bEndpointAddress),
		controller->in.buffer, ep_in->wMaxPacketSize, 
		xpad360wr_receive, controller, ep_in->bInterval
	);
	
	usb_fill_int_urb(
		controller->out.urb, usbdev,
		usb_sndintpipe(usbdev, ep_out->bEndpointAddress),
		controller->out.buffer, ep_out->wMaxPacketSize,
		xpad360_common_complete, controller, ep_out->bInterval
	);

	controller->in.urb->transfer_dma = controller->in.dma;
	controller->in.urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	
	controller->out.urb->transfer_dma = controller->out.dma;
	controller->out.urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* Populate device capabilities */
#define SET_BIT(type) __set_bit(type, controller->inputdev->keybit);

	__set_bit(EV_KEY, controller->inputdev->evbit); /* General device that has key presses. */
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
	__set_bit(type, controller->inputdev->absbit);\
	input_set_abs_params(controller->inputdev, type, -32768, 32767, 16, 128);

	/* Axis... */
	__set_bit(EV_ABS, controller->inputdev->evbit);
	SET_BIT(ABS_X);
	SET_BIT(ABS_Y);
	SET_BIT(ABS_RX);
	SET_BIT(ABS_RY);

#undef SET_BIT
#define SET_BIT(type) \
	__set_bit(type, controller->inputdev->absbit); \
	input_set_abs_params(controller->inputdev, type, 0, 255, 0, 0);

	/* Triggers... */
	SET_BIT(ABS_Z);
	SET_BIT(ABS_RZ);

#undef SET_BIT
#define SET_BIT(type) __set_bit(type, controller->inputdev->ffbit)

	/* Force Feedback */
	__set_bit(EV_FF, controller->inputdev->evbit);
	SET_BIT(FF_RUMBLE);

#undef SET_BIT

	/* Branch code slightly based on wired and wireles. Based on bInterfaceProtocol. */
	if (protocol == 129) {
		__set_bit(BTN_TRIGGER_HAPPY1, controller->inputdev->keybit);
		__set_bit(BTN_TRIGGER_HAPPY2, controller->inputdev->keybit);
		__set_bit(BTN_TRIGGER_HAPPY3, controller->inputdev->keybit);
		__set_bit(BTN_TRIGGER_HAPPY4, controller->inputdev->keybit);
		xpad360wr_query_presence(controller);
		
		error = input_ff_create_memless(controller->inputdev, NULL, xpad360wr_rumble);
		if (error) {
			dev_dbg(device, "input_ff_create_memless() failed!\n");
			input_ff_destroy(controller->inputdev);
			error = 0; /* We can live without FF support. */
		}
	}
	else if (protocol == 1) {
		__set_bit(ABS_HAT0X, controller->inputdev->absbit); 
		__set_bit(ABS_HAT0Y, controller->inputdev->absbit); 
		input_set_abs_params(controller->inputdev, ABS_HAT0X, -1, 1, 0, 0);
		input_set_abs_params(controller->inputdev, ABS_HAT0Y, -1, 1, 0, 0);
		controller->present = true;
		controller->in.urb->complete = xpad360_receive;
		xpad360_set_led(controller, XPAD360_LED_ON_1);
		
		/* Unfortunately, I know of no simple way to change LED based on how many are connected without HID. */
		error = input_ff_create_memless(controller->inputdev, NULL, xpad360_rumble);
		if (error) {
			dev_dbg(device, "input_ff_create_memless() failed!\n");
			input_ff_destroy(controller->inputdev);
			error = 0; /* We can live without FF support. */
		}
		
		/* Wired controller only connects once. */
		error = input_register_device(controller->inputdev);
		if (unlikely(error)) {
			dev_dbg(device, "input_register_device() failed!\n");
			goto fail5;
		}
	}
	
	error = usb_submit_urb(controller->in.urb, GFP_KERNEL);
	if (unlikely(error)) {
		dev_dbg(device, "usb_submit_urb(controller->in.urb) failed!\n");
		goto fail4;
	}

	return 0;

fail5:
	usb_free_urb(controller->out.urb);
fail4:
	input_ff_destroy(controller->inputdev); 
	input_free_device(controller->inputdev);
fail3:
	usb_free_urb(controller->in.urb);
fail2:
	usb_free_coherent(
		usbdev,
		ep_out->wMaxPacketSize,
		controller->out.buffer,
		controller->out.dma
	);
fail1:
	usb_free_coherent(
		usbdev,
		ep_in->wMaxPacketSize,
		controller->in.buffer,
		controller->in.dma
	);
fail0:
	kfree(controller);
	return error;
}

void xpad360_common_disconnect(struct usb_interface* interface)
{
	struct xpad360_controller *controller = usb_get_intfdata(interface);
	struct usb_device *usbdev = interface_to_usbdev(interface);
	struct device *device = &(controller->usbintf->dev);
	int protocol = interface->cur_altsetting->desc.bInterfaceProtocol;
	
	dev_dbg(device, "Controller disconnected.\n");

	flush_scheduled_work();

	if (controller->present) {
		input_unregister_device(controller->inputdev);
		
		if (usbdev->state != USB_STATE_NOTATTACHED ) {
			if (protocol == 129)
				xpad360wr_set_led_sync(controller, XPAD360_LED_ROTATING);
			else if (protocol == 1) {
				xpad360_set_led_sync(controller, XPAD360_LED_ROTATING);
			}
		}
	}
	
	usb_kill_urb(controller->in.urb);
	usb_kill_urb(controller->out.urb);
	
	usb_free_coherent(
		usbdev,
		interface->cur_altsetting->endpoint[0].desc.wMaxPacketSize,
		controller->in.buffer,
		controller->in.dma
	);

	usb_free_coherent(
		usbdev,
		interface->cur_altsetting->endpoint[1].desc.wMaxPacketSize,
		controller->out.buffer,
		controller->out.dma
	);

	usb_free_urb(controller->in.urb);
	usb_free_urb(controller->out.urb);
	
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