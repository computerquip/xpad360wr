#include "xpad360c.h"

MODULE_AUTHOR("Zachary Lund <admin@computerquip.com>");
MODULE_DESCRIPTION("Xbox 360 Wireless Adapter");
MODULE_LICENSE("GPL");

struct xpad360wr_controller {
	struct xpad360_controller xpad; /* Allows us to cast into an xpad360_controller */

	struct mutex mutex;

	struct packet_work packet_work;

	const char *name;
	uint8_t num_controller; /* This can be calculated from interface. This is just for convenience. */
};

static const char* xpad360wr_device_names[] = {
	"Xbox 360 Wireless Adapter",
};

static const struct usb_device_id xpad360wr_table[] = {
	{ USB_DEVICE_INTERFACE_PROTOCOL(0x045E, 0x0719, 129) },
	{}
};

static void xpad360wr_query_presence(struct xpad360_controller *controller)
{
	struct urb *urb = xpad360c_copy_urb(controller->out, GFP_ATOMIC);
	u8 *buffer = urb->transfer_buffer;
	
	static const u8 packet[12] = {
		0x08, 0x00, 0x0F, 0xC0,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00
	};
	
	memcpy(buffer, packet, sizeof(packet));

	controller->out->transfer_buffer_length = 12;
	
	usb_submit_urb(controller->out, GFP_ATOMIC);
}

static void _xpad360wr_generate_led_packet(void* buffer, u8 stat, u8 test)
{
	/* test does something.. haven't figured it out yet. */
	const u8 packet[10] = {
		0x00, 0x00, test, stat, 0x40 + (stat % 14),
		0x00, 0x00, 0x00, 0x00, 0x00
	};
	
	memcpy(buffer, packet, sizeof(packet));
}

void xpad360wr_led_sync(struct xpad360_controller *controller, u8 status)
{
	struct usb_device *usbdev = controller->out->dev; 
	u8 packet[10];

	_xpad360wr_generate_led_packet(packet, status, 0x08);

	usb_interrupt_msg(
		usbdev,	controller->out->pipe,
		packet, sizeof(packet), NULL, 0
	);
}

/* We don't currently use this... as it was causing data races (I had to wrap it up in mutexes anyways...)
 * Although, perhaps later if I can find optimizations in design without issues */
void xpad360wr_led(struct xpad360_controller *controller, enum xpad360c_led_t status)
{
	struct urb *urb = xpad360c_copy_urb(controller->out, GFP_ATOMIC);
	
	_xpad360wr_generate_led_packet(urb->transfer_buffer, status, 0x08);
	urb->transfer_buffer_length = 10;

	usb_submit_urb(urb, GFP_ATOMIC);
}

int xpad360wr_rumble(struct input_dev *dev, void *stuff, struct ff_effect *effect)
{
	struct xpad360_controller *controller = (struct xpad360_controller*)stuff;

	if (effect->type == FF_RUMBLE) {
		struct urb *urb = xpad360c_copy_urb(controller->out, GFP_ATOMIC);

		u8 left = effect->u.rumble.strong_magnitude / 255;
		u8 rite = effect->u.rumble.weak_magnitude / 255;

		u8 packet[12] = {
			0x00, 0x01, 0x0F, 0xC0, 
			0x00, left, rite, 0x00, 
			0x00, 0x00, 0x00, 0x00
		};

		memcpy(urb->transfer_buffer, packet, sizeof(packet)); 
		urb->transfer_buffer_length = 12;

		return usb_submit_urb(urb, GFP_ATOMIC);
	} else return -1;
}

void xpad360wr_register_input(struct xpad360wr_controller *wr_controller, struct usb_device *usbdev)
{
	struct xpad360_controller *controller = &wr_controller->xpad;
	struct input_dev *inputdev;
	int error = 0;
	
	xpad360c_allocate_inputdev(
		controller, usbdev,
		wr_controller->name,
		controller->path);
	
	if (!controller->inputdev) return;

	inputdev = controller->inputdev;

	/* Wireless specific stuff */
	__set_bit(BTN_TRIGGER_HAPPY1, inputdev->keybit);
	__set_bit(BTN_TRIGGER_HAPPY2, inputdev->keybit);
	__set_bit(BTN_TRIGGER_HAPPY3, inputdev->keybit);
	__set_bit(BTN_TRIGGER_HAPPY4, inputdev->keybit);
	
	input_ff_create_memless(inputdev, NULL, xpad360wr_rumble);

	error = input_register_device(inputdev);

	if (unlikely(error)) {
		input_free_device(inputdev);
		controller->inputdev = NULL;
	}
}

void xpad360wr_process_packet_work(struct work_struct* work) 
{
	struct packet_work *packet = (struct packet_work*)work;
	struct xpad360wr_controller *controller = packet->urb->context;
	struct device *device = &packet->urb->dev->dev;
	struct input_dev *inputdev = controller->xpad.inputdev;
	u8 *data = packet->urb->transfer_buffer;
	size_t data_length = packet->urb->actual_length;

	/* Event from Adapter */
	if (data[0] == 0x08 && data_length == 2) {
		mutex_lock(&controller->mutex);

		switch (data[1]) {
		case 0x00:
			/* All flags off */
			xpad360c_destroy_inputdev(&controller->xpad);
			break;

		case 0xC0:
			/* Connection + Headset flag */
		case 0x80: {
			xpad360wr_led(&controller->xpad, controller->num_controller + 6);
			xpad360wr_register_input(controller, packet->urb->dev);
			break;
		}

		case 0x40:
			/* Headset flag */
			break;
		}

		mutex_unlock(&controller->mutex);
	}
	/* Event from Controller */
	else if (data[0] == 0x00 && data_length == 29) {
		u16 header = le16_to_cpup((__le16*)&data[1]);

		switch (header) {
		case 0x0000: /* FIXME */
			break;
			
		case 0x0001:
			/* The only time this will not lock is during disconnection. */
			if (!mutex_trylock(&controller->mutex)){
				dev_dbg(device, "Tried to acquire mutex while it was "
							  	"locked during input parsing!");
				break;
			}

			if (!inputdev) {
				dev_dbg(device, "Input event recieved without input device initialized!\n");
				goto input_proc_finish;
			}
			
			input_report_key(inputdev, BTN_TRIGGER_HAPPY3, data[6] & 0x01); /* D-pad up	 */
			input_report_key(inputdev, BTN_TRIGGER_HAPPY4, data[6] & 0x02); /* D-pad down */
			input_report_key(inputdev, BTN_TRIGGER_HAPPY1, data[6] & 0x04); /* D-pad left */
			input_report_key(inputdev, BTN_TRIGGER_HAPPY2, data[6] & 0x08); /* D-pad right */
			xpad360c_parse_input(inputdev, &data[6]);
			
input_proc_finish:
			mutex_unlock(&controller->mutex);

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
		case 0x01F8: /* FIXME */
		case 0x02F8: /* FIXME */
			break;
		case 0x000F:
			/* Announce packet... still needs to be reverse engineered... FIX ME */
			dev_dbg(device,
				"Serial: %2x:%2x:%2x:%2x:%2x:%2x:%2x\n",
				data[7], data[8], data[9], data[10], data[11], data[12], data[13]
			       );
			dev_dbg(device, "Battery Status: %i\n", data[17]);
			break;
		default:
			dev_dbg(device, "Unknown packet receieved. Header was %#.8x\n", header);
		}
	}
#ifdef DEBUG 
	else {
		/* FIXME: Smaller way to do the following, perhaps with device info? */
		int i = 0;
		
		printk(
			KERN_DEBUG 
			"Unknown packet received. "
			"Header %#.2x "
			"Packet: ",
			data[0] 
      		);
		
		for (; i < data_length; ++i) 
			printk(KERN_CONT "%#x ", (unsigned int)data[i]);
	}
#endif

	xpad360c_destroy_urb(packet->urb);
}

void xpad360wr_receive(struct urb *urb)
{
	struct xpad360wr_controller *controller = urb->context;
	struct device *device = &urb->dev->dev;
	
	if (!xpad360c_check_urb(urb))
		return;
	
	/* The scheduled work will clean the urb up. */
	controller->packet_work.urb = controller->xpad.in;
	schedule_work((struct work_struct*)&controller->packet_work);

	/* Don't wait, just create a new one and resubmit. */
	controller->xpad.in = xpad360c_copy_urb(controller->xpad.in, GFP_ATOMIC);
	
	if (!controller->xpad.in) {
		dev_err(device, "Failed to recreate the in urb!");
		return;
	}

	if (unlikely(usb_submit_urb(controller->xpad.in, GFP_ATOMIC) != 0))
		dev_err(device, "usb_submit_urb() failed in receive()!");
}

int xpad360wr_probe(struct usb_interface *interface, const struct usb_device_id *id)
{	
	int error = 0;

	/* Wireless controller specific initialization.  */
	struct xpad360wr_controller *controller = 
		kzalloc(sizeof(struct xpad360wr_controller), GFP_KERNEL);

	usb_set_intfdata(interface, controller);

	mutex_init(&controller->mutex);
	INIT_WORK((struct work_struct *)&controller->packet_work, xpad360wr_process_packet_work);
	
	controller->num_controller = (interface->cur_altsetting->desc.bInterfaceNumber + 1) / 2;
	controller->name = xpad360wr_device_names[id - xpad360wr_table];
	
	{
		char tmp[8];
		char * path = controller->xpad.path;
		const size_t size = sizeof(controller->xpad.path);

		snprintf(tmp, sizeof(tmp), "/input%.1i", controller->num_controller);
		usb_make_path(interface_to_usbdev(interface), path, size);
		strlcat(path, tmp, size);
	}
	
	error = 
	xpad360c_probe(
		(struct xpad360_controller*)controller, 
		interface,
		xpad360wr_receive,
		xpad360c_dangerous_complete
	);

	if (error) return error;

	error = usb_submit_urb(controller->xpad.in, GFP_KERNEL);
	if (unlikely(error)) {
		goto fail;
	}

	xpad360wr_query_presence(&controller->xpad);

	goto success;
	
fail:
	xpad360c_destroy(&controller->xpad);
success:
	return error;
}

void xpad360wr_disconnect(struct usb_interface *interface)
{	
	struct xpad360wr_controller *controller = usb_get_intfdata(interface);
	struct usb_device *usbdev = interface_to_usbdev(interface);

	xpad360c_destroy(&controller->xpad);
	flush_work((struct work_struct*)&controller->packet_work);
	
	if (controller->xpad.inputdev) {
		xpad360c_destroy_inputdev(&controller->xpad);
		
		if (usbdev->state != USB_STATE_NOTATTACHED)
			xpad360wr_led_sync(&controller->xpad, XPAD360_LED_ROTATING);
	}

	kfree(controller);
}

static struct usb_driver xpad360wr_driver = {
	.name		= "xpad360wr",
	.probe		= xpad360wr_probe,
	.disconnect	= xpad360wr_disconnect,
	.id_table	= xpad360wr_table,
	.soft_unbind	= 1 /* Allows us to set LED properly before module unload. */
};

MODULE_DEVICE_TABLE(usb, xpad360wr_table);
module_usb_driver(xpad360wr_driver);
