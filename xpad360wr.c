#include "xpad360-common.h"

void xpad360wr_query_presence(struct xpad360_controller *controller)
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
	u8 *data = _data;
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
void xpad360wr_set_led_sync(struct xpad360_controller *controller, u8 status)
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

void xpad360wr_set_led(struct xpad360_controller *controller, u8 status)
{
	u8 *data = controller->out.buffer;
	
	_xpad360wr_generate_led_packet(data, status, 0x08);
	controller->out.urb->transfer_buffer_length = 10;

	if (unlikely(usb_submit_urb(controller->out.urb, GFP_ATOMIC) != 0)) {
		dev_dbg(&(controller->usbintf->dev), "usb_submit_urb() failed in set_led()!");
	}
}

int xpad360wr_rumble(struct input_dev *dev, void *stuff, struct ff_effect *effect)
{
	struct xpad360_controller *controller = input_get_drvdata(dev);

	if (effect->type == FF_RUMBLE) {
		u8 *data = controller->out.buffer;
		u16 strong = effect->u.rumble.strong_magnitude;
		u16 weak = effect->u.rumble.weak_magnitude;

		data[0] = 0x00;
		data[1] = 0x01;
		data[2] = 0x0F;
		data[3] = 0xC0;
		data[4] = 0x00;
		data[5] = (u8)(strong / 255); /* Left */
		data[6] = (u8)(weak / 255); /* Right */
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

void xpad360wr_receive(struct urb *urb)
{
	struct xpad360_controller *controller = urb->context;
	unsigned char* data = controller->in.buffer;
	struct device *device = &(controller->usbintf->dev);

	CHECK_URB_STATUS(urb)

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
			dev_dbg(device, "Reporting %i as D-pad up", data[0] & 0x01);
			input_report_key(controller->inputdev, BTN_TRIGGER_HAPPY3, data[6] & 0x01); /* D-pad up	 */
			input_report_key(controller->inputdev, BTN_TRIGGER_HAPPY4, data[6] & 0x02); /* D-pad down */
			input_report_key(controller->inputdev, BTN_TRIGGER_HAPPY1, data[6] & 0x04); /* D-pad left */
			input_report_key(controller->inputdev, BTN_TRIGGER_HAPPY2, data[6] & 0x08); /* D-pad right */
			xpad360_common_parse_input(controller, &data[6]);
			break;
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
		dev_dbg(device, "usb_submit_urb() failed in receive()!");
	}
}