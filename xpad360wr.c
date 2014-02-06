#include "xpad360-common.h"

/* TODO: Synchronize rumble with input mutex, or rather, figure out if it needs it. */

void xpad360wr_query_presence(struct xpad360_controller *controller)
{
	u8 *data = controller->out_presence.buffer;
	
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
	controller->out_presence.urb->transfer_buffer_length = 12;
	
	if (unlikely(usb_submit_urb(controller->out_presence.urb, GFP_ATOMIC) != 0)) {
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
	u8 *data = controller->out_led.buffer;
	
	_xpad360wr_generate_led_packet(data, status, 0x08);
	controller->out_led.urb->transfer_buffer_length = 10;

	if (unlikely(usb_submit_urb(controller->out_led.urb, GFP_ATOMIC) != 0)) {
		dev_dbg(&(controller->usbintf->dev), "usb_submit_urb() failed in set_led()!");
	}
}

int xpad360wr_rumble(struct input_dev *dev, void *stuff, struct ff_effect *effect)
{
	struct xpad360_controller *controller = input_get_drvdata(dev);

	if (effect->type == FF_RUMBLE) {
		u8 *data = controller->out_rumble.buffer;
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
		controller->out_rumble.urb->transfer_buffer_length = 12;
	} else return 1;

	if (unlikely(usb_submit_urb(controller->out_rumble.urb, GFP_ATOMIC) != 0)) {
		dev_dbg(&(controller->usbintf->dev), "usb_submit_urb() failed in play_effect()!");
		return -1;
	}

	return 0;
}

static void xpad360wr_register_input_work(struct work_struct* work)
{
	struct input_work *inputwork = (struct input_work*)work;
	struct xpad360_input *input = inputwork->input;
	struct device *device = &inputwork->usbintf->dev;
	int error = 0;
	
	/* If an input event is going off before this happens,
	   the input event will fail because it's null. It won't
	   be able to do anything until input_allocate_device() is called.
	   During that time, it's okay to use the input device for anything
	   although some calls my be a noop. */
	
	if (input->dev) {
		/* This should actually never happen */
		dev_dbg(device, "Device attempt to register a non-NULL device!");
		return
	}
	
	dev_info(device, "Registering input device...");
	
	input->dev = input_allocate_device();
	
	if (unlikely(input->dev == NULL)) {
		dev_err(device, "input_allocate_device failed!\n");
		return;
	}
	
	xpad360_common_init_input_dev(input->dev, inputwork->usbintf);
	__set_bit(BTN_TRIGGER_HAPPY1, input->dev->keybit);
	__set_bit(BTN_TRIGGER_HAPPY2, input->dev->keybit);
	__set_bit(BTN_TRIGGER_HAPPY3, input->dev->keybit);
	__set_bit(BTN_TRIGGER_HAPPY4, input->dev->keybit);

	error = input_ff_create_memless(input->dev, NULL, xpad360wr_rumble);
	if (error) {
		dev_err(device, "input_ff_create_memless() failed!\n");
		error = 0; /* We can live without FF support. */
	}
	
	error = input_register_device(input->dev);

	if (unlikely(error)) {
		dev_err(device, "input_register_device() failed!\n");
		return;
	}
}

static void xpad360wr_unregister_input_work(struct work_struct* work)
{
	struct input_work *inputwork = (struct input_work*)work;
	struct xpad360_input *input = inputwork->input;
	struct device *device = &inputwork->usbintf->dev;

	/* The only time this will not lock is during an input event. */
	mutex_lock(&input->mutex);
	
	if (!input->dev) {
		dev_dbg(device, "Device attempted to unregister a NULL device.");
		goto success;
	}
	
	dev_info(device, "Unregistering input device...");
	input_unregister_device(input->dev);
	input->dev = NULL;
	
success:
	mutex_unlock(&input->mutex);
}

static void xpad360wr_process_input_work(struct work_struct* work) 
{
	struct input_work *inputwork = (struct input_work*)work;
	struct xpad360_input *input = inputwork->input;
	struct device *device = &inputwork->usbintf->dev;
	u8 *data = inputwork->request->buffer;
	
	/* The only time this will not lock is during disconnection. */
	mutex_lock(&input->mutex);

	if (!input->dev) {
		dev_dbg(device, "Input event recieved without input device initialized!\n");
		goto success;
	}
	
	input_report_key(input->dev, BTN_TRIGGER_HAPPY3, data[6] & 0x01); /* D-pad up	 */
	input_report_key(input->dev, BTN_TRIGGER_HAPPY4, data[6] & 0x02); /* D-pad down */
	input_report_key(input->dev, BTN_TRIGGER_HAPPY1, data[6] & 0x04); /* D-pad left */
	input_report_key(input->dev, BTN_TRIGGER_HAPPY2, data[6] & 0x08); /* D-pad right */
	xpad360_common_parse_input(input->dev, &data[6]);
	
	xpad360_common_destroy_request(
		inputwork->request, 
		inputwork->usbintf,
		XPAD360_EP_IN
	);

	kfree(inputwork->request);
	
success:
	mutex_unlock(&input->mutex);
}

void xpad360wr_receive(struct urb *urb)
{
	struct xpad360_controller *controller = urb->context;
	u8* data = controller->in->buffer;
	struct device *device = &(controller->usbintf->dev);

	if (!controller) {
		dev_err(device, "Controller is null!");
		return;
	}
	
	CHECK_URB_STATUS(urb)
	
	/* Input synchronization is kinda a big deal here... because we offput
	   syncronization to a handler, I'm concerned that even though event A
	   showed up before event B, event B might be slightly processed faster than
	   event A in certain cases. I'd like some assurance otherwise... */
	
	/* Event from Wireless Receiver */
	if (data[0] == 0x08 && urb->actual_length == 2) {
		switch (data[1]) {
		case 0x00:
			schedule_work((struct work_struct *)&controller->unregister_input);
			dev_info(device, "Controller has been disconnected!\n");
			break;

		case 0x80:
			xpad360wr_set_led(controller, controller->num_controller + 6);
			schedule_work((struct work_struct *)&controller->register_input);
			dev_info(device, "Controller has been connected!\n");
			break;

		case 0x40:
			dev_info(device, "Controller has connected a headset!\n");
			break;

		case 0xC0:

			xpad360wr_set_led(controller, controller->num_controller + 6);
			schedule_work((struct work_struct *)&controller->register_input);
			/* TODO: Schedule something for headsets. */
			dev_info(device, "Controller has connected with a headset!\n");
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
			break;
		case 0x0001: 
			/* Input events occur *a lot*. Is it okay to use kzalloc like this? 
			 * The only real alter*/
			controller->process_input.request = controller->in;
			schedule_work((struct work_struct *)&controller->process_input);
			
			controller->in = kzalloc(sizeof(struct xpad360_request), GFP_ATOMIC);
			
			error = 
			xpad360_common_init_request(
				controller->in, 
				controller->usbintf, 
				XPAD360_EP_IN, 
				xpad360wr_receive,
				GFP_ATOMIC
			);
			
			if (error) {
				dev_dbg(device, "Generating new 'in' request failed. Should probably die. FIXME");
				break;
			}
			
			break;
		}
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
		int i = 0;
		
		printk(KERN_DEBUG "Unknown packet received: ");
		
		for (; i < urb->actual_length; ++i) 
			printk(KERN_CONT "%#x ", (unsigned int)data[i]);
	}

	if (unlikely(usb_submit_urb(controller->in->urb, GFP_ATOMIC) != 0)) {
		dev_err(device, "usb_submit_urb() failed in receive()!");
	}
}

int xpad360wr_init(struct xpad360_controller *controller)
{	
	struct device *device = &(controller->usbintf->dev);
	int error = 0;

	mutex_init(&controller->input.mutex);
	
	/* *_input.request is generated during xpad360wr_receive */
	controller->register_input.usbintf = controller->usbintf;
	controller->unregister_input.usbintf = controller->usbintf;
	controller->process_input.usbintf = controller->usbintf;
	
	controller->unregister_input.input = &controller->input;
	controller->register_input.input = &controller->input;
	controller->process_input.input = &controller->input;
	
	INIT_WORK((struct work_struct *)&controller->register_input, xpad360wr_register_input_work);
	INIT_WORK((struct work_struct *)&controller->unregister_input, xpad360wr_unregister_input_work);
	INIT_WORK((struct work_struct *)&controller->process_input, xpad360wr_process_input_work);
	
	controller->num_controller = (controller->usbintf->cur_altsetting->desc.bInterfaceNumber + 1) / 2;
	
	{
		char tmp[8];
		snprintf(tmp, sizeof(tmp), "/input%.1i", controller->num_controller);
		usb_make_path(interface_to_usbdev(controller->usbintf), controller->path, sizeof(controller->path));
		strlcat(controller->path, tmp, sizeof(controller->path));
	}
	
	controller->in = kzalloc(sizeof(struct xpad360_request), GFP_KERNEL);
	
	error = xpad360_common_init_request(
		controller->in,
		controller->usbintf,
		XPAD360_EP_IN,
		xpad360wr_receive,
		GFP_KERNEL
	);
	
	if (error) {
		dev_err(device, "controller->in failed to init!");
		return error;
	}
	
	error = xpad360_common_init_request(
		&controller->out_presence,
		controller->usbintf,
		XPAD360_EP_OUT,
		NULL, GFP_KERNEL
	);
	
	if (error) {
		dev_err(device, "controller->out_presence failed to init!");
		goto fail;
	}
	
	xpad360wr_query_presence(controller);
	
	goto success;
	
fail:
	xpad360_common_destroy_request(
		controller->in,
		controller->usbintf,
		XPAD360_EP_IN
	);
success:
	return error;
}

void xpad360wr_destroy(struct xpad360_controller *controller)
{
	struct usb_device *usbdev = interface_to_usbdev(controller->usbintf);
	
	xpad360_common_destroy_request(
		&controller->out_presence,
		controller->usbintf,
		XPAD360_EP_OUT
	);
	
	usb_poison_urb(controller->in);
	
	if (controller->input.dev) {
		input_unregister_device(controller->input.dev);
		controller->input.dev = NULL;
		
		if (usbdev->state != USB_STATE_NOTATTACHED)
			xpad360wr_set_led_sync(controller, XPAD360_LED_ROTATING);
	}
}