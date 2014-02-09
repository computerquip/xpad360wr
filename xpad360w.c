#include "xpad360-common.h"

int xpad360_rumble(struct input_dev *dev, void* stuff, struct ff_effect *effect)
{
	struct xpad360_controller *controller = input_get_drvdata(dev);

	if (effect->type == FF_RUMBLE) {
		u8 *data = controller->out_rumble.buffer;
		u16 strong = effect->u.rumble.strong_magnitude;
		u16 weak = effect->u.rumble.weak_magnitude;
		
		data[0] = 0x00;
		data[1] = 0x08;
		data[2] = 0x00;
		data[3] = (u8)(strong / 255);
		data[4] = (u8)(weak / 255);
		data[5] = 0x00;
		data[6] = 0x00;
		data[7] = 0x00;
		controller->out_rumble.urb->transfer_buffer_length = 8;
	}
	else return -1;
	
	if (unlikely(usb_submit_urb(controller->out_rumble.urb, GFP_ATOMIC) != 0)) {
		dev_dbg(&(controller->usbintf->dev), "usb_submit_urb() failed in play_effect()!");
		return -1;
	}
	
	return 0;
}

/* Data must be a buffer with 3 writeable bytes ahead of it!*/
static void _xpad360_generate_led_packet(void* _data, u8 status)
{
	u8 *data = _data;
	data[0] = 0x01;
	data[1] = 0x03;
	data[2] = status;
}

void xpad360_set_led_sync(struct xpad360_controller *controller, u8 status) {
	struct usb_endpoint_descriptor * usbep = &(controller->usbintf->cur_altsetting->endpoint[1].desc);
	struct usb_device *usbdev = interface_to_usbdev(controller->usbintf);
	u8 data[3];
	int actual_length = 0;
	int error = 0;
	
	_xpad360_generate_led_packet(data, status);
	
	error = 
	usb_interrupt_msg(
		usbdev,	usb_sndintpipe(usbdev, usbep->bEndpointAddress),
		data, sizeof(data), &actual_length, 0
	);
	
	if (error) {
		dev_dbg(&(controller->usbintf->dev), "synchronous set_led function failed!");
	}
}

void xpad360_set_led(struct xpad360_controller *controller, u8 status) 
{
	u8 *data = controller->out_led.buffer;
	
	_xpad360_generate_led_packet(data, status);
	controller->out_led.urb->transfer_buffer_length = 3;

	if (unlikely(usb_submit_urb(controller->out_led.urb, GFP_ATOMIC) != 0)) {
		dev_dbg(&(controller->usbintf->dev), "usb_submit_urb() failed in set_led()!");
	}
}

void xpad360_receive(struct urb* urb) {
	struct xpad360_controller *controller = urb->context;
	unsigned char* data = controller->in->buffer;
	struct device *device = &(controller->usbintf->dev);
	struct input_dev *inputdev = controller->input.dev;
	u16 header;

	CHECK_URB_STATUS(device, urb)
	
	header = le16_to_cpup((__le16*)&data[0]);
	switch (header) {
	case 0x0301:
		dev_dbg(device, "Controller LED status: %i\n", data[2]);
		break;
	case 0x0303:
		/* Some HID something or other... blah */
		dev_dbg(device, "Rumble packet or something... I dunno. Have some info: %i\n", data[2]);
		break;
	case 0x0308:
		dev_dbg(device, "Attachment attached! We don't support any of them. );");
		break;
	case 0x1400:
		/* Since nothing else can possibly inputdev, no need to lock. */
		input_report_abs(inputdev, ABS_HAT0X, !!(data[2] & 0x08) - !!(data[2] & 0x04));
		input_report_abs(inputdev, ABS_HAT0Y, !!(data[2] & 0x02) - !!(data[2] & 0x01));
		xpad360_common_parse_input(controller->input.dev, &data[2]);
		break;
	default: 
		dev_dbg(device, "Unknown packet received: "
				"Header: %#.4x "
				"Data: %#x",
				header,
				(unsigned int)*data
		);
		
	}
	
	if (unlikely(usb_submit_urb(urb, GFP_ATOMIC) != 0)) {
		dev_dbg(device, "usb_submit_urb() failed in receive()!");
	}
}
int xpad360_init(struct xpad360_controller *controller)
{
	struct device *device = &(controller->usbintf->dev);
	int error = 0;
	
	dev_dbg(device, "Initializing xpad360 wired controller...");
	
	usb_make_path(interface_to_usbdev(controller->usbintf), controller->path, sizeof(controller->path));
	strlcat(controller->path, "/input0", sizeof(controller->path));
	
	/* Wired controller only connects once. */
	controller->input.dev = input_allocate_device();
	if (unlikely(controller->input.dev == NULL)) {
		dev_dbg(device, "input_allocate_device failed!\n");
		return -ENOMEM;
	}
	
	error = input_ff_create_memless(controller->input.dev, NULL, xpad360_rumble);
	if (error) {
		dev_dbg(device, "input_ff_create_memless() failed!\n");
		input_ff_destroy(controller->input.dev);
		error = 0; /* We can live without FF support. */
	}
	
	xpad360_common_init_input_dev(controller->input.dev, controller->usbintf);
	input_set_abs_params(controller->input.dev, ABS_HAT0X, -1, 1, 0, 0);
	input_set_abs_params(controller->input.dev, ABS_HAT0Y, -1, 1, 0, 0);
	__set_bit(ABS_HAT0X, controller->input.dev->absbit); 
	__set_bit(ABS_HAT0Y, controller->input.dev->absbit);

	error = input_register_device(controller->input.dev);
	if (unlikely(error)) {
		dev_dbg(device, "input_register_device() failed!\n");
		goto fail;
	}
	
	controller->in = kzalloc(sizeof(struct xpad360_request), GFP_KERNEL);
	
	error = xpad360_common_init_request(
		controller->in,
		controller->usbintf,
		XPAD360_EP_IN,
		xpad360_receive,
		GFP_KERNEL
	);
	
	if (error) {
		dev_dbg(device, "controller->in failed to init!");
		goto fail;
	}
	
	xpad360_set_led(controller, XPAD360_LED_ON_1);
	
	goto success;
	
fail:
	input_free_device(controller->input.dev);
success:
	return error;
}

void xpad360_destroy(struct xpad360_controller *controller) 
{
	struct usb_device *usbdev = interface_to_usbdev(controller->usbintf);
	input_unregister_device(controller->input.dev);
		
	if (usbdev->state != USB_STATE_NOTATTACHED )
		xpad360_set_led_sync(controller, XPAD360_LED_ROTATING);
}