/*
	TODO:
	Headsets
	Add support for wired xbox 360 controllers
	Perhaps recreate this driver using hid_driver but:
		1) Need to recreate report descriptors and,
		2) HID protocol is rather complicated... :/
	
	NOTE:
	Shoutout to xpad and xboxdrv for whatever work is resembling theirs.
*/


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb/input.h>

MODULE_AUTHOR("Zachary Lund <admin@computerquip.com>");
MODULE_DESCRIPTION("Xbox 360 Wireless Adapter");
MODULE_LICENSE("GPL");

enum {
	XPAD360WR_LED_OFF,
	XPAD360WR_LED_ALL_BLINKING,
	XPAD360WR_LED_FLASH_ON_1,
	XPAD360WR_LED_FLASH_ON_2,
	XPAD360WR_LED_FLASH_ON_3,
	XPAD360WR_LED_FLASH_ON_4,
	XPAD360WR_LED_ON_1,
	XPAD360WR_LED_ON_2,
	XPAD360WR_LED_ON_3,
	XPAD360WR_LED_ON_4,
	XPAD360WR_LED_ROTATING,
	XPAD360WR_LED_SECTIONAL_BLINKING,
	XPAD360WR_LED_SLOW_SECTIONAL_BLINKING,
	XPAD360WR_LED_ALTERNATING
};

static struct usb_device_id xpad360_table[] = {
	{ USB_DEVICE_INTERFACE_PROTOCOL(0x045E, 0x0719, 129) },
	{}
};

struct xpad360_request {
	dma_addr_t dma;
	void *buffer;
	struct urb *urb;
};

struct xpad360_controller {
	u8 num_controller;

	struct input_dev *inputdev;
	struct usb_interface *usbintf;

	bool present;

	struct xpad360_request in;
	struct xpad360_request out;

	char path[64]; /* Physical stable path we can reference to */
};

static void xpad360wr_query_presence(struct xpad360_controller *controller)
{
	u8 *data = controller->out.buffer;
	
	data[0] = 0x08;
	data[1] = 0x00;
	data[2] = 0x0F;
	data[3] = 0xC0;
	data[4] = 0x00;
	data[5] = 0x00;
	data[6] = 0x00;
	data[7] = 0x00;
	data[8] = 0x00;
	data[9] = 0x00;
	data[10] = 0x00;
	data[11] = 0x00;
	controller->out.urb->transfer_buffer_length = 12;
	
	if (unlikely(usb_submit_urb(controller->out.urb, GFP_ATOMIC) != 0)) {
		dev_dbg(&(controller->usbintf->dev), "usb_submit_urb() failed in query_presence()!");
	}
}

/* Data must be a buffer with 10 writeable bytes ahead of it! */
static void _xpad360wr_generate_led_packet(void* _data, u8 status, u8 test)
{
	/* test does something.. haven't figured it out yet. */
	u8* data = _data;
	data[0] = 0x00;
	data[1] = 0x00;
	data[2] = test;
	data[3] = (u8)0x40 + (status % 14);
	data[4] = 0x00;
	data[5] = 0x00;
	data[6] = 0x00;
	data[7] = 0x00;
	data[8] = 0x00;
	data[9] = 0x00;
}

/* Must not be called in interrupt context */
static void xpad360wr_set_led_sync(struct xpad360_controller *controller, u8 status)
{
	struct usb_endpoint_descriptor * usbep = &(controller->usbintf->cur_altsetting->endpoint[1].desc);
	struct usb_device *usbdev = interface_to_usbdev(controller->usbintf);
	u8 data[10];
	int actual_length = 0;
	int error = 0;

	_xpad360wr_generate_led_packet(data, status, 0x08);
	
	error = 
	usb_interrupt_msg(
		usbdev,	usb_sndintpipe(usbdev, usbep->bEndpointAddress),
		data, sizeof(data), &actual_length, 0
	);
	
	if (error) {
		dev_dbg(&(controller->usbintf->dev), "synchronous set_led function failed!");
	}
}

static void xpad360wr_set_led(struct xpad360_controller *controller, u8 status)
{
	u8 *data = controller->out.buffer;
	
	_xpad360wr_generate_led_packet(data, status, 0x08);
	controller->out.urb->transfer_buffer_length = 10;

	if (unlikely(usb_submit_urb(controller->out.urb, GFP_ATOMIC) != 0)) {
		dev_dbg(&(controller->usbintf->dev), "usb_submit_urb() failed in set_led()!");
	}
}

static int xpad360wr_rumble(struct input_dev *dev, void *stuff, struct ff_effect *effect)
{
	struct xpad360_controller *controller = input_get_drvdata(dev);
	u8 *data = controller->out.buffer;

	if (effect->type == FF_RUMBLE) {
		u16 strong = effect->u.rumble.strong_magnitude;
		u16 weak = effect->u.rumble.weak_magnitude;

		data[0] = 0x00;
		data[1] = 0x01;
		data[2] = 0x0F;
		data[3] = 0xC0;
		data[4] = 0x00;
		data[5] = strong / 255; /* Left */
		data[6] = weak / 255; /* Right */
		data[7] = 0x00;
		data[8] = 0x00;
		data[9] = 0x00;
		data[10] = 0x00;
		data[11] = 0x00;
		controller->out.urb->transfer_buffer_length = 12;
	} else return 1;

	if (unlikely(usb_submit_urb(controller->out.urb, GFP_ATOMIC) != 0)) {
		dev_dbg(&(controller->usbintf->dev), "usb_submit_urb() failed in play_effect()!");
		return -1;
	}

	return 0;
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

static void xpad360wr_generic_complete(struct urb *urb)
{
	struct xpad360_controller *controller = urb->context;
	struct device *device = &(controller->usbintf->dev);

	switch (urb->status) {
	case 0:
		dev_dbg(device, "Sent message to controller successfully!");
		return;
	case -ECONNRESET:
		dev_dbg(device, "Controller has been reset.\n");
		return;
	case -ESHUTDOWN:
		dev_dbg(device, "Controller has shutdown.\n");
		return;
	case -ENOENT:
		dev_dbg(device, "Controller has been poisoned.\n");
		return;
	default:
		dev_dbg(device, "Unknown status returned by controller: %x\n", urb->status);
		return;
	}
}

static void xpad360wr_receive(struct urb *urb)
{
	struct xpad360_controller *controller = urb->context;
	struct input_dev *inputdev = controller->inputdev;
	unsigned char* data = controller->in.buffer;
	struct device *device = &(controller->usbintf->dev);

	switch (urb->status) {
	case 0:
		break;
	case -ECONNRESET:
		dev_dbg(device, "Controller has been reset.\n");
		return;
	case -ESHUTDOWN:
		dev_dbg(device, "Controller has shutdown.\n");
		return;
	case -ENOENT:
		dev_dbg(device, "Controller has been poisoned.\n");
		return;
	default:
		dev_dbg(device, "Unknown status returned by controller: %x\n", urb->status);
		return;
	}

	/* Event from Wireless Receiver */
	if (data[0] == 0x08 && urb->actual_length == 2) {
		switch (data[1]) {
		case 0x00:
			/* Controller disconnected */
			controller->present = false;
			dev_dbg(device, "Controller has been disconnected!\n");
			break;

		case 0x80:
			/* Controller connected */
			xpad360wr_set_led(controller, controller->num_controller + 6);
			controller->present = true;
			dev_dbg(device, "Controller has been connected!\n");
			break;

		case 0x40:
			/* Headset connected */
			dev_dbg(device, "Controller has connected a headset!\n");
			break;

		case 0xC0:
			/* Controller with headset connect */
			controller->present = true;
			dev_dbg(device, "Controller has connected with a headset!\n");
			break;
		default:
			dev_dbg(device, "Unknown packet received. Length was 2, header was %#.2x\n", data[1]);
		}
	}
	/* Event from Controller */
	else if (data[0] == 0x00 && urb->actual_length == 29) {
		u16 header = le16_to_cpup((__le16*)&data[1]);

		switch (header) {
		case 0x0000:
			/* Doesn't seem to mean anything... maybe HID related? */
			break;
		case 0x0001:
			/* Event report */
			/* data[5] is packet size including the byte itself, which we don't use */
			/* data[18] and past are padding perhaps used with other devices. */

#if 1
			input_report_key(inputdev, BTN_TRIGGER_HAPPY3, data[6] & 0x01); /* D-pad up	 */
			input_report_key(inputdev, BTN_TRIGGER_HAPPY4, data[6] & 0x02); /* D-pad down */
			input_report_key(inputdev, BTN_TRIGGER_HAPPY1, data[6] & 0x04); /* D-pad left */
			input_report_key(inputdev, BTN_TRIGGER_HAPPY2, data[6] & 0x08); /* D-pad right */
#else
			input_report_key(inputdev, BTN_DPAD_UP, data[6] & 0x01); /* D-pad up	 */
			input_report_key(inputdev, BTN_DPAD_DOWN, data[6] & 0x02); /* D-pad down */
			input_report_key(inputdev, BTN_DPAD_LEFT, data[6] & 0x04); /* D-pad left */
			input_report_key(inputdev, BTN_DPAD_RIGHT, data[6] & 0x08); /* D-pad right */
#endif 

			/* start/back buttons */
			input_report_key(inputdev, BTN_START,  data[6] & 0x10);
			input_report_key(inputdev, BTN_SELECT, data[6] & 0x20); /* Back */

			/* stick press left/right */
			input_report_key(inputdev, BTN_THUMBL, data[6] & 0x40);
			input_report_key(inputdev, BTN_THUMBR, data[6] & 0x80);

			input_report_key(inputdev, BTN_TL,	data[7] & 0x01); /* Left Shoulder */
			input_report_key(inputdev, BTN_TR,	data[7] & 0x02); /* Right Shoulder */
			input_report_key(inputdev, BTN_MODE,	data[7] & 0x04); /* Guide */
			/* data[8] & 0x08 is a dummy value */
			input_report_key(inputdev, BTN_A,	data[7] & 0x10);
			input_report_key(inputdev, BTN_B,	data[7] & 0x20);
			input_report_key(inputdev, BTN_X,	data[7] & 0x40);
			input_report_key(inputdev, BTN_Y,	data[7] & 0x80);

			/* triggers */
#if 1
			input_report_abs(inputdev, ABS_Z, data[8]);
			input_report_abs(inputdev, ABS_RZ, data[9]);
#else /* This code was something I tested with Psychonauts... it makes the camera work out of the box but I still can't figure out wth they are expecting.  */
			{ 
				int left = ~(data[8] / 2);
				int right = (data[9] / 2);
				input_report_abs(inputdev, ABS_Z, (left + right) + 128);
			}
#endif

			/* left stick */
			input_report_abs(inputdev, ABS_X, (s16)le16_to_cpup((__le16*)&data[10]));
			input_report_abs(inputdev, ABS_Y, ~(s16)le16_to_cpup((__le16*)&data[12]));

			/* right stick */
			input_report_abs(inputdev, ABS_RX, (s16)le16_to_cpup((__le16*)&data[14]));
			input_report_abs(inputdev, ABS_RY, ~(s16)le16_to_cpup((__le16*)&data[16]));

			input_sync(inputdev);

			break;

		/* The following two packets are for some reason sent twice...? */
		case 0x000A: {
			int size = (strchr((char*)&data[5], 0xFF) - (char*)&data[5]);
			dev_dbg(device, "Controller has attachment! Description: %.*s\n", size, (char*)&data[5]);
			
			break;
		}
		case 0x0009:
			/* This appears when an attachment is connected. It contains the serial barcode on the back of attachment. */
			dev_dbg(device, "Attachment Serial: %.14s\n", (char*)&data[5]);
			break;
		case 0x01F8:
		case 0x02F8:
			break;
		case 0x000F:
			/* First packet sent by controller when connected. */
			dev_dbg(device,
				"Serial: %2x:%2x:%2x:%2x:%2x:%2x:%2x\n",
				data[7], data[8], data[9], data[10], data[11], data[12], data[13]
			       );
			dev_dbg(device, "Battery Status: %i\n", data[17]);
			break;
		default:
			dev_dbg(device, "Unknown packet receieved. Header was %#.8x\n", header);
		}
	} else {
		/* No known case of this happening. */
		dev_dbg(device, "Unknown packet received. Length was %i... what about the header...?", urb->actual_length);
	}

	if (unlikely(usb_submit_urb(urb, GFP_ATOMIC) != 0)) {
		dev_dbg(&device, "usb_submit_urb() failed in receive()!");
	}
}

int xpad360_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device * usbdev = interface_to_usbdev(interface);
	struct usb_endpoint_descriptor *ep_in = &(interface->cur_altsetting->endpoint[0].desc);
	struct usb_endpoint_descriptor *ep_out = &(interface->cur_altsetting->endpoint[1].desc);
	struct xpad360_controller *controller = kzalloc(sizeof(struct xpad360_controller), GFP_KERNEL);
	struct device *device = &(interface->dev);
	//int protocol = interface->cur_altsetting->desc.bInterfaceProtocol;
	int error = 0;
	
	dev_dbg(device, "Device: %s\nSerial: %s\nUsage: %i", usbdev->product, usbdev->serial, interface->pm_usage_cnt.counter);
	
	if (!controller) {
		return -ENOMEM;
	}
	
	controller->usbintf = interface;
	
	/* Allocate input structure */
	controller->inputdev = input_allocate_device();
	
	if (unlikely(controller->inputdev == NULL)) {
		error = -ENOMEM;
		goto fail0;
	}
	
	/* Allocate ff structure */
	error = input_ff_create_memless(controller->inputdev, NULL, xpad360wr_rumble);

	/* We can live without FF support. */
	if (unlikely(error)) {
		dev_dbg(device, "input_ff_create_memless() failed!\n");
		input_ff_destroy(controller->inputdev);
		error = 0;
	}

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
		xpad360wr_generic_complete, controller, ep_out->bInterval
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
	SET_BIT(BTN_TRIGGER_HAPPY1);
	SET_BIT(BTN_TRIGGER_HAPPY2);
	SET_BIT(BTN_TRIGGER_HAPPY3);
	SET_BIT(BTN_TRIGGER_HAPPY4);
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
	
	/* Register device so the input subsystem sees it. */
	error = input_register_device(controller->inputdev);
	if (unlikely(error)) {
		dev_dbg(device, "input_register_device() failed!\n");
		goto fail4;
	}

	/* Immediately start reading packets so we can catch our presence packet. */
	error = usb_submit_urb(controller->in.urb, GFP_KERNEL);
	if (unlikely(error)) {
		dev_dbg(device, "usb_submit_urb(controller->in.urb) failed!\n");
		goto fail5;
	}
	
	/* This will cause the reciever to send a connection packet which our callback will handle. */
	xpad360wr_query_presence(controller); /* No big deal if this fails.  */

	return 0;

fail5:
	input_free_device(controller->inputdev);
fail4:
	usb_free_urb(controller->out.urb);
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
	input_ff_destroy(controller->inputdev); 

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

void xpad360_disconnect(struct usb_interface* interface)
{
	struct xpad360_controller *controller = usb_get_intfdata(interface);
	struct usb_device *usbdev = interface_to_usbdev(interface);
	struct device *device = &(controller->usbintf->dev);
	
	dev_dbg(device, "Controller disconnected.\n");
	
	if (controller->present && usbdev->state != USB_STATE_NOTATTACHED) {
		xpad360wr_set_led_sync(controller, XPAD360WR_LED_OFF);
	}
	
	usb_kill_urb(controller->in.urb);
	usb_kill_urb(controller->out.urb);
	
	input_unregister_device(controller->inputdev);

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
	.probe		= xpad360_probe,
	.disconnect	= xpad360_disconnect,
	.id_table	= xpad360_table,
	.soft_unbind	= 1
};

MODULE_DEVICE_TABLE(usb, xpad360_table);
module_usb_driver(xpad360_driver);
