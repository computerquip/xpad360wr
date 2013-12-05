#define DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb/input.h>

MODULE_AUTHOR("Zachary Lund <admin@computerquip.com>");
MODULE_DESCRIPTION("Xbox 360 Wireless Adapter");
MODULE_LICENSE("GPL");

/*
	LED status definitions in incremental order:
	OFF = 0x00,
	BLINKING,
	FLASH_1_ON,
	FLASH_2_ON,
	FLASH_3_ON,
	FLASH_4_ON,
	ON_1,
	ON_2,
	ON_3,
	ON_4,
	ROTATING,
	SECTIONAL_BLINKING,
	SLOW_SECTIONAL_BLINKING,
	ALTERNATING
*/

/*
	TODO:
	I need to figure out what the rest of these damn packets do.
	Headsets, although I will implement that in a different driver when I get to it.
	Add support for wired xbox 360 controllers (will do whenever I get mine in the mail). 
*/
#define MAX_PACKET_SIZE 32

static struct usb_device_id xpad360wr_table[] = {
	{
		USB_DEVICE_INTERFACE_PROTOCOL(0x045e, 0x0719, 129)
	},
	{}
};
struct xpad360wr_buffer {
	dma_addr_t dma; /*  */
	void *buffer;
};

struct xpad360wr_controller {
	u8 num_controller;

	struct input_dev *inputdev; /* input subsystem device */
	struct usb_interface *usbintf; /* usb subsystem interface */
	struct usb_device *usbdev; /* usb subsystem device... perhaps not needed since can be obtained from interface */

	bool present;

	struct xpad360wr_buffer ep_in;
	struct xpad360wr_buffer ep_out;

	struct urb *irq_in;
	struct urb *irq_out;

	char path[64]; /* Physical stable path we can reference to */
};

static void xpad360wr_controller_query_presence(struct xpad360wr_controller *controller)
{
	u8 *data = controller->ep_out.buffer;
	
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
	controller->irq_out->transfer_buffer_length = 12;
	
	if (unlikely(usb_submit_urb(controller->irq_out, GFP_ATOMIC) != 0)) {
		dev_dbg(&(controller->usbintf->dev), "usb_submit_urb() failed in query_presence()!");
	}
}

/* Data must be a buffer with 10 writeable bytes ahead of it! */
static void _xpad360wr_generate_led_packet(void* _data, u8 status)
{
	/* Values higher than 13 defaults to 1 apparently (by hardware).  */
	/* Also, setting data[2] to 0x00 and status to 0x00 causes rather strange behaviors... */
	
	u8* data = _data;
	data[0] = 0x00;
	data[1] = 0x00;
	data[2] = 0x08;
	data[3] = (u8)(0x40 + status);
	data[4] = 0x00;
	data[5] = 0x00;
	data[6] = 0x00;
	data[7] = 0x00;
	data[8] = 0x00;
	data[9] = 0x00;
}

/* Must not be called in interrupt context */
static void xpad360wr_controller_set_led_sync(struct xpad360wr_controller *controller, u8 status)
{
	struct usb_endpoint_descriptor * usbep = &(controller->usbintf->cur_altsetting->endpoint[1].desc);
	u8 data[10];
	int actual_length = 0;
	int error = 0;

	_xpad360wr_generate_led_packet(data, status);
	
	error = 
	usb_interrupt_msg(
		controller->usbdev,
		usb_sndintpipe(controller->usbdev, usbep->bEndpointAddress),
		data, sizeof(data), &actual_length, 0
	);
	
	if (error) {
		dev_dbg(&(controller->usbintf->dev), "synchronous set_led function failed!");
	}
}

static void xpad360wr_controller_set_led(struct xpad360wr_controller *controller, u8 status)
{
	u8 *data = controller->ep_out.buffer;
	
	_xpad360wr_generate_led_packet(data, status);
	controller->irq_out->transfer_buffer_length = 10;

	if (unlikely(usb_submit_urb(controller->irq_out, GFP_ATOMIC) != 0)) {
		dev_dbg(&(controller->usbintf->dev), "usb_submit_urb() failed in set_led()!");
	}
}

/* Force Feedback Play Effect */
static int xpad360wr_controller_play_effect(struct input_dev *dev, void *stuff, struct ff_effect *effect)
{
	struct xpad360wr_controller *controller = input_get_drvdata(dev);
	u8 *data = controller->ep_out.buffer;

	if (effect->type == FF_RUMBLE) {
		u16 strong = effect->u.rumble.strong_magnitude;
		u16 weak = effect->u.rumble.weak_magnitude;

		/* Verbatim xboxdrv */
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
		controller->irq_out->transfer_buffer_length = 12;
	} else return 1;

	if (unlikely(usb_submit_urb(controller->irq_out, GFP_ATOMIC) != 0)) {
		dev_dbg(&(controller->usbintf->dev), "usb_submit_urb() failed in play_effect()!");
		return -1;
	}

	return 0;
}

static int xpad360wr_controller_open(struct input_dev* dev)
{
	struct xpad360wr_controller *controller = input_get_drvdata(dev);
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

static void xpad360wr_controller_close(struct input_dev* dev)
{
	struct xpad360wr_controller *controller = input_get_drvdata(dev);
	struct device *device = &(controller->usbintf->dev);

	dev_dbg(device, "Closing controller.");
	/* We cannot stop inquiring packets as connection packets are sent from the same interface. */
}

static void xpad360wr_irq_send(struct urb *urb)
{
	struct xpad360wr_controller *controller = urb->context;
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

static void xpad360wr_irq_receive(struct urb *urb)
{
	struct xpad360wr_controller *controller = urb->context;
	struct input_dev *inputdev = controller->inputdev;
	unsigned char* data = controller->ep_in.buffer;
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

	/*  NOTE:
	 * 		Some bytes sent from some controller event (specifically data[2] and data[3]) are unknown.
	 * 		They change on every single packet (excluding 0x0000 packets which doesn't appear to have data[2] or data[3])
	 * 		However, the same numbers re appear so it does not appear to be completely random.
	 * 		Some packets are apparently excepted from this behavior by providing "0xF0".
	 * 		Regardless, they don't seem neccessary for decent functionality. Would be nice to know though.
	 *
	 * 		There are two packets, 0x02F8 and 0x01F8, that have unknown use. They alternate in when they are sent.
	 * 		0x01F8 is always sent first with a following 0x02F8 packet. This seems to be some sort of ping mechanism.
	 * 		Also seems safe to ignore.
	 */

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
			xpad360wr_controller_set_led(controller, controller->num_controller + 2);
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
			/* This packet is sent in many variants, none of which seem to mean a damn thing */
			/* Although, hinting something is a version of this packet (with data[3] being 0xF0) consistently being sent after button/analog events. */
			/* Maybe it marks the end of an event */

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

			input_report_abs(inputdev, ABS_Z, data[8]);
			input_report_abs(inputdev, ABS_RZ, data[9]);

			/* left stick */
			input_report_abs(inputdev, ABS_X, (s16)le16_to_cpup((__le16*)&data[10]));
			input_report_abs(inputdev, ABS_Y, ~(s16)le16_to_cpup((__le16*)&data[12]));

			/* right stick */
			input_report_abs(inputdev, ABS_RX, (s16)le16_to_cpup((__le16*)&data[14]));
			input_report_abs(inputdev, ABS_RY, ~(s16)le16_to_cpup((__le16*)&data[16]));

			input_sync(inputdev);

			break;

			/* The following two packets are for some reason sent twice... purpose? */
		case 0x000A:
			/* This appears to be sent when a controller attachment is connected */
			/* The string seems to be delimited with 0xFF...
			 * ...that seems really stupid so hopefully the multiple 0xFF is actually something else.  */
		{
			int size = (strchr((char*)&data[5], 0xFF) - (char*)&data[5]);
			dev_dbg(device, "Controller has attachment! Description: %.*s\n", size, (char*)&data[5]);
		}
		break;
		case 0x0009:
			/* This appears when an attachment is connected. It contains the serial barcode on the back of attachment. */
			/* There are some characters past serial, I do not know what they do or are. */
			dev_dbg(device, "Attachment Serial: %.14s\n", (char*)&data[5]);
			break;
		case 0x01F8:
		case 0x02F8:
			break;
		case 0x000F:
#if 0
			/* First packet sent by controller when connected. */
			dev_dbg(device,
				/* This is from Xboxdrv...
				 * not sure the validity or indication that this is actually the serial or battery status.*/
				"Serial: %2x:%2x:%2x:%2x:%2x:%2x:%2x\n",
				data[7], data[8], data[9], data[10], data[11], data[12], data[13]
			       );
			dev_dbg(device, "Battery Status: %i\n", data[17]);
#endif
			break;
		default:
			dev_dbg(device, "Unknown packet receieved. Header was %#.8x\n", header);
		}
	} else {
		/* No known case of this happening. */
		dev_dbg(device, "Unknown packet received. Length was %i... what about the header...?", urb->actual_length);
	}

	usb_submit_urb(urb, GFP_ATOMIC);
}

int xpad360wr_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device * usbdev = interface_to_usbdev(interface);
	struct usb_endpoint_descriptor *usbep = &(interface->cur_altsetting->endpoint[0].desc);
	struct xpad360wr_controller *controller = kzalloc(sizeof(struct xpad360wr_controller), GFP_KERNEL);
	struct device *device = &(interface->dev);
	int error = 0;
	
	if (!controller) {
		return -ENOMEM;
	}
	
	controller->usbdev = usbdev;
	controller->usbintf = interface;

	/* Is this the most reliable method of fetching controller number? */
	controller->num_controller = (interface->cur_altsetting->desc.bInterfaceNumber + 1) / 2;

	controller->inputdev = input_allocate_device();
	if (unlikely(controller->inputdev == NULL)) {
		error = -ENOMEM;
		goto fail0;
	}

	/* Initialize in endpoint */
	controller->ep_in.buffer =
		usb_alloc_coherent(
			usbdev,
			MAX_PACKET_SIZE,
			GFP_KERNEL,
			&(controller->ep_in.dma)
		);

	if (unlikely(!controller->ep_in.buffer)) {
		error = -ENOMEM;
		goto fail0;
	}

	controller->irq_in = usb_alloc_urb(0, GFP_KERNEL);

	if (unlikely(!controller->irq_in)) {
		error = -ENOMEM;
		goto fail1;
	}

	{
		char tmp[8];
		/* Could this potentionally cause overflow? */
		snprintf(tmp, sizeof(tmp), "/input%.1i", controller->num_controller);
		usb_make_path(usbdev, controller->path, sizeof(controller->path));
		strlcat(controller->path, tmp, sizeof(controller->path));
	}

	controller->inputdev->name = "Xbox 360 Wireless Adapter"; 	/* Programmatically fetch idProduct string? */
	controller->inputdev->phys = controller->path; 				/* Probably more issues here... */
	controller->inputdev->dev.parent = device;
	controller->inputdev->open = xpad360wr_controller_open;
	controller->inputdev->close = xpad360wr_controller_close;

	usb_to_input_id(usbdev, &controller->inputdev->id);

	input_set_drvdata(controller->inputdev, controller);
	usb_set_intfdata(interface, controller);

	usb_fill_int_urb(
		controller->irq_in, usbdev,
		usb_rcvintpipe(usbdev, usbep->bEndpointAddress),
		controller->ep_in.buffer, MAX_PACKET_SIZE, xpad360wr_irq_receive,
		controller, usbep->bInterval
	);

	controller->irq_in->transfer_dma
		= controller->ep_in.dma;
	controller->irq_in->transfer_flags
	|= URB_NO_TRANSFER_DMA_MAP;

	/* Initialize out endpoint */
	usbep = &(interface->cur_altsetting->endpoint[1].desc);

	controller->ep_out.buffer =
		usb_alloc_coherent(
			usbdev,
			MAX_PACKET_SIZE,
			GFP_KERNEL,
			&(controller->ep_out.dma)
		);

	if (unlikely(!controller->ep_out.buffer)) {
		error = -ENOMEM;
		goto fail2;
	}

	controller->irq_out = usb_alloc_urb(0, GFP_KERNEL);

	if (unlikely(!controller->irq_out)) {
		error = -ENOMEM;
		goto fail3;
	}

	usb_fill_int_urb(
		controller->irq_out, usbdev,
		usb_sndintpipe(usbdev, usbep->bEndpointAddress),
		controller->ep_out.buffer, MAX_PACKET_SIZE, xpad360wr_irq_send,
		controller, usbep->bInterval
	);

	controller->irq_out->transfer_dma = controller->ep_out.dma;
	controller->irq_out->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

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
#if 1 /* Currently broken, causes null pointer dereference somewhere... */
#define SET_BIT(type) __set_bit(type, controller->inputdev->ffbit)

	/* Force Feedback */
	__set_bit(EV_FF, controller->inputdev->evbit);
	SET_BIT(FF_RUMBLE);

#undef SET_BIT

	error = input_ff_create_memless(controller->inputdev, NULL, xpad360wr_controller_play_effect);

	if (unlikely(error)) {
		dev_dbg(device, "input_ff_create_memless() failed!\n");
		input_ff_destroy(controller->inputdev);
		/* Remove capability so we don't fool applications */
		__clear_bit(FF_RUMBLE, controller->inputdev->ffbit);
		__clear_bit(EV_FF, controller->inputdev->evbit);
		error = 0;
	}
#endif

	error = input_register_device(controller->inputdev);
	if (unlikely(error)) {
		dev_dbg(device, "input_register_device() failed!\n");
		goto fail5;
	}

	controller->irq_in->dev = usbdev;
	error = usb_submit_urb(controller->irq_in, GFP_KERNEL);
	if (unlikely(error)) {
		dev_dbg(device, "usb_submit_urb(controller->irq_in) failed!\n");
		goto fail6;
	}
	
	xpad360wr_controller_query_presence(controller); /* No big deal if this fails.  */

	return 0;

fail6:
	usb_free_urb(controller->irq_out);
fail5:
	input_ff_destroy(controller->inputdev); /* Is this actually required? */
	input_free_device(controller->inputdev);
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
	kfree(controller);
	return error;
}

void xpad360wr_disconnect(struct usb_interface* interface)
{
	struct xpad360wr_controller *controller = usb_get_intfdata(interface);
	struct usb_device *usbdev = controller->usbdev;
	struct device *device = &(controller->usbintf->dev);
	
	dev_dbg(device, "Controller disconnected.\n");
	
	if (controller->present && usbdev->state != USB_STATE_NOTATTACHED) {
		xpad360wr_controller_set_led_sync(controller, 1);
	}
	
	usb_kill_urb(controller->irq_in);
	usb_kill_urb(controller->irq_out);
	
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
}

static struct usb_driver xpad360wr_driver = {
	.name		= "xpad360wr",
	.probe		= xpad360wr_probe,
	.disconnect	= xpad360wr_disconnect,
	.id_table	= xpad360wr_table,
	.soft_unbind	= 1
};

MODULE_DEVICE_TABLE(usb, xpad360wr_table);
module_usb_driver(xpad360wr_driver);
