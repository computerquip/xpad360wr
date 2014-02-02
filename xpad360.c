#include "xpad360-common.h"

int xpad360_rumble(struct input_dev *dev, void* stuff, struct ff_effect *effect)
{
	struct xpad360_controller *controller = input_get_drvdata(dev);

	if (effect->type == FF_RUMBLE) {
		u8 *data = controller->out.buffer;
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
		controller->out.urb->transfer_buffer_length = 8;
	}
	else return -1;
	
	if (unlikely(usb_submit_urb(controller->out.urb, GFP_ATOMIC) != 0)) {
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
	u8 *data = controller->out.buffer;
	
	_xpad360_generate_led_packet(data, status);
	controller->out.urb->transfer_buffer_length = 3;

	if (unlikely(usb_submit_urb(controller->out.urb, GFP_ATOMIC) != 0)) {
		dev_dbg(&(controller->usbintf->dev), "usb_submit_urb() failed in set_led()!");
	}
}

void xpad360_receive(struct urb* urb) {
	struct xpad360_controller *controller = urb->context;
	unsigned char* data = controller->in.buffer;
	struct device *device = &(controller->usbintf->dev);
	struct input_dev *inputdev = controller->inputdev;
	u16 header;

	CHECK_URB_STATUS(urb)
	
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
		input_report_abs(inputdev, ABS_HAT0X, !!(data[2] & 0x08) - !!(data[2] & 0x04));
		input_report_abs(inputdev, ABS_HAT0Y, !!(data[2] & 0x02) - !!(data[2] & 0x01));
		xpad360_common_parse_input(controller, &data[2]);
		dev_dbg(device, "Length was %i\n", urb->actual_length);
		break;
	default:
		dev_dbg(device, "Unknown packet received. Length was %i... what about the header...?", urb->actual_length);
	}
	
	if (unlikely(usb_submit_urb(urb, GFP_ATOMIC) != 0)) {
		dev_dbg(device, "usb_submit_urb() failed in receive()!");
	}
}