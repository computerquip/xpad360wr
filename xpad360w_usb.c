#include "xpad360c.h"

MODULE_AUTHOR("Zachary Lund <admin@computerquip.com>");
MODULE_DESCRIPTION("Xbox 360 Wired Controllers");
MODULE_LICENSE("GPL");

static const char* xpad360w_device_names[] = {
	"Xbox 360 Wired Controller",
};

static struct usb_device_id xpad360w_table[] = {
	{ USB_DEVICE_INTERFACE_PROTOCOL(0x045E, 0x028e, 1) },
	{}
};

static int xpad360w_rumble(struct input_dev *dev, void* stuff, struct ff_effect *effect)
{
	struct xpad360_controller *controller = stuff;

	if (effect->type == FF_RUMBLE) {
		struct urb *urb = xpad360c_copy_urb(controller->out, GFP_ATOMIC);

		u8 left = effect->u.rumble.strong_magnitude / 255;
		u8 rite = effect->u.rumble.weak_magnitude / 255;
		
		const u8 packet[8] = { 
			0x00, 0x08, 0x00, 
			left, rite,
			0x00, 0x00, 0x00 
		};

		memcpy(urb->transfer_buffer, packet, sizeof(packet));

		urb->transfer_buffer_length = 8;

		usb_anchor_urb(urb, &controller->out_anchor);

		return !!usb_submit_urb(urb, GFP_ATOMIC);
	} else return -1;
}

/* Data must be a buffer with 3 writeable bytes ahead of it!*/
static void xpad360w_generate_led_packet(void* buffer, u8 status)
{
	const u8 packet[3] = { 0x01, 0x03, status };
	memcpy(buffer, packet, sizeof(packet));
}

static void xpad360w_led_sync(struct xpad360_controller *controller, u8 status) {
	struct usb_device *usbdev = controller->out->dev;
	struct device *device = &usbdev->dev;
	u8 packet[3];
	int error = 0;
	
	xpad360w_generate_led_packet(packet, status);
	
	error = 
	usb_interrupt_msg(
		usbdev,	controller->out->pipe,
		packet, sizeof(packet), NULL, 0
	);
	
	if (error) {
		dev_dbg(device, "synchronous set_led function failed!");
	}
}

static void xpad360w_led(struct xpad360_controller *controller, u8 status) 
{
	struct urb *urb = xpad360c_copy_urb(controller->out, GFP_ATOMIC);
	struct device *device = &urb->dev->dev;
	
	xpad360w_generate_led_packet(urb->transfer_buffer, status);
	urb->transfer_buffer_length = 3;

	usb_anchor_urb(urb, &controller->out_anchor);

	if (unlikely(usb_submit_urb(urb, GFP_ATOMIC) != 0)) {
		dev_dbg(device, "usb_submit_urb() failed in set_led()!");
	}
}

static void xpad360w_receive(struct urb* urb) {
	struct xpad360_controller *controller = urb->context;
	struct device *device = &urb->dev->dev;
	struct input_dev *inputdev = controller->inputdev;
	u8* data = controller->in->transfer_buffer;
	u16 header;

	if (!xpad360c_check_urb(urb))
		return;

	header = le16_to_cpup((__le16*)&data[0]);
	switch (header) {

	case 0x0301:
		dev_dbg(device, "Controller LED status: %i\n", data[2]);
		break;
	case 0x0303:
		dev_dbg(device, "Rumble packet or something... I dunno. Have some info: %i\n", data[2]);
		break;
	case 0x0308:
		dev_dbg(device, "Attachment attached! We don't support any of them. );");
		break;
	case 0x1400:
		if (!inputdev) {
			dev_dbg(device, "Attempted to use inputdev while NULL!");
			break;
		}

		input_report_abs(inputdev, ABS_HAT0X, !!(data[2] & 0x08) - !!(data[2] & 0x04));
		input_report_abs(inputdev, ABS_HAT0Y, !!(data[2] & 0x02) - !!(data[2] & 0x01));
		xpad360c_parse_input(inputdev, &data[2]);
		break;
	default: 
		dev_dbg(device, "Unknown packet received: "
				"Header: %#.4x", header);
		
	}

	if (unlikely(usb_submit_urb(urb, GFP_ATOMIC) != 0)) {
		dev_dbg(device, "usb_submit_urb() failed in receive()!");
	}
}

static void xpad360w_register_input(
	struct xpad360_controller *controller,
	struct usb_device *usbdev,
	const char *name,
	const char *path)
{
	struct input_dev *inputdev;
	int error = 0;

	dev_dbg(&usbdev->dev, 
		"Registering Input:\n"
		"\tName: %s\n"
		"\tPath: %s\n",
		name, path);
	
	xpad360c_allocate_inputdev(controller, usbdev, name, path);
	inputdev = controller->inputdev;
	if (!inputdev) return;

	/* TODO: Check validity, if bad, remove from feature bit. */
	input_ff_create_memless(inputdev, controller, xpad360w_rumble);

	/* Wireless specific stuff */
	input_set_abs_params(inputdev, ABS_HAT0X, -1, 1, 0, 0);
	input_set_abs_params(inputdev, ABS_HAT0Y, -1, 1, 0, 0);
	__set_bit(ABS_HAT0X, inputdev->absbit); 
	__set_bit(ABS_HAT0Y, inputdev->absbit);

	error = input_register_device(inputdev);

	if (unlikely(error)) {
		input_free_device(inputdev);
		controller->inputdev = NULL;
	}
}

static int xpad360w_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *usbdev = interface_to_usbdev(interface);
	struct xpad360_controller *controller = 
		devm_kzalloc(&usbdev->dev, sizeof(struct xpad360_controller), GFP_KERNEL);

	int error = 0;

	if (!controller)
		return -ENOMEM;	

	usb_set_intfdata(interface, controller);

	usb_make_path(usbdev, controller->path, sizeof(controller->path));

#if 1
	dev_dbg(&usbdev->dev, "Device Name: %s\n", xpad360w_device_names[id - xpad360w_table]);

	xpad360w_register_input(
		controller, usbdev,
		xpad360w_device_names[id - xpad360w_table],
		controller->path
	);

	if (!controller->inputdev) {
		error = -ENOMEM;
		goto fail0;
	}
#endif

#if 1
	error = 
	xpad360c_probe(
		controller, 
		interface, 
		xpad360w_receive, 
		xpad360c_dangerous_complete);

	if (error) goto fail1;

	xpad360w_led(controller, XPAD360_LED_ON_1);
#endif	

	goto success;

	
fail1:
	input_unregister_device(controller->inputdev);
fail0:
	devm_kfree(&usbdev->dev, controller); /* Is this needed? */
success:
	return error;
}

static void xpad360w_disconnect(struct usb_interface *interface)
{
	struct usb_device *usbdev = interface_to_usbdev(interface);
	struct xpad360_controller *controller = usb_get_intfdata(interface);

#if 1
	usb_kill_urb(controller->in);	
	usb_kill_anchored_urbs(&controller->out_anchor);

	if (usbdev->state != USB_STATE_NOTATTACHED)
		xpad360w_led_sync(controller, XPAD360_LED_ROTATING);

	xpad360c_destroy(controller);
#endif

#if 1
	input_unregister_device(controller->inputdev);
#endif
}

static struct usb_driver xpad360w_driver = {
	.name		= "xpad360w",
	.probe		= xpad360w_probe,
	.disconnect	= xpad360w_disconnect,
	.id_table	= xpad360w_table,
	.soft_unbind	= 1 /* Allows us to set LED properly before module unload. */
};

MODULE_DEVICE_TABLE(usb, xpad360w_table);
module_usb_driver(xpad360w_driver);
