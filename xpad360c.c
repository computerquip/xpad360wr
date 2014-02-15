/*
	TODO:
	Headsets

	NOTES:
	I'm forgetting the idea that the controller is HID compliant. 
	While other drivers do use a filter driver for HID, we do not as it's inconvenient. 
	It doesn't make anything less painful, especially since Linux doesn't have a HID filter driver
		and we'd have to provide our own HID descriptor. 

	All allocation functions also initialize (which is always what we want). 
*/
#include "xpad360c.h"

static int xpad360c_controller_open(struct input_dev* inputdev)
{ return 0; }

static void xpad360c_controller_close(struct input_dev* inputdev)
{}

static void xpad360c_input_capabilities(struct input_dev *inputdev) 
{
	/* TODO: Fix this ugly mess. Send all data at once. 
	   That would be cleaner and much more efficient.  */
#define SET_BIT(type) __set_bit(type, inputdev->keybit);

	/* Buttons */
	__set_bit(EV_KEY, inputdev->evbit);
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

	/* Axis (Sticks)*/
	__set_bit(EV_ABS, inputdev->evbit);
	SET_BIT(ABS_X);
	SET_BIT(ABS_Y);
	SET_BIT(ABS_RX);
	SET_BIT(ABS_RY);

#undef SET_BIT
#define SET_BIT(type) \
	__set_bit(type, inputdev->absbit); \
	input_set_abs_params(inputdev, type, 0, 255, 0, 0);

	/* Triggers */
	SET_BIT(ABS_Z);
	SET_BIT(ABS_RZ);

#undef SET_BIT

	/* Force Feedback */
	__set_bit(EV_FF, inputdev->evbit);
	__set_bit(FF_RUMBLE, inputdev->ffbit);
}

/* There is only one input_dev per controller, ever.
   So, we just pass a controller instead of doing it more flexibly.  */
void xpad360c_allocate_inputdev(struct xpad360_controller *controller)
{
	struct input_dev * inputdev = input_allocate_device();
	struct usb_device *usbdev = controller->in->dev;

	if (!inputdev){
		controller->inputdev = NULL;
		return;
	}

	inputdev->name = controller->name;
	inputdev->phys = controller->path;
	inputdev->dev.parent = &usbdev->dev; /* This may be unneccessary.  */
	inputdev->open = xpad360c_controller_open;
	inputdev->close = xpad360c_controller_close;

	xpad360c_input_capabilities(inputdev);
	usb_to_input_id(usbdev, &inputdev->id);

	controller->inputdev = inputdev;
}

void xpad360c_destroy_inputdev(struct xpad360_controller *controller)
{
	if (!controller->inputdev)
		return;

	input_unregister_device(controller->inputdev);
	controller->inputdev = NULL;
}

void xpad360c_complete(struct urb *urb)
{
	/* We don't do anything because we're not an actual module...
	   This is probably not ideal.  */
}

/* 
 * This function is similar for all 360 controllers, only with different offsets. 
 * Anything uncommon is dealt with in specific modules.
 * Each specific module has to deal with its own quirks. 
 */
void xpad360c_parse_input(struct input_dev *inputdev, void *_data) 
{
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

	/* Left Stick */
	input_report_abs(inputdev, ABS_X, (s16)le16_to_cpup((__le16*)&data[4]));
	input_report_abs(inputdev, ABS_Y, ~(s16)le16_to_cpup((__le16*)&data[6]));

	/* Right Stick */
	input_report_abs(inputdev, ABS_RX, (s16)le16_to_cpup((__le16*)&data[8]));
	input_report_abs(inputdev, ABS_RY, ~(s16)le16_to_cpup((__le16*)&data[10]));
	
	input_sync(inputdev);
}

/* This allocates and initializes an urb specific for our needs. */
struct urb* xpad360c_allocate_urb(
	struct usb_device *usbdev,
	int pipe, /* We can construct an endpoint from a pipe... but not the other way around. */ 
	void(*callback)(struct urb*),
	gfp_t mem_flags)
{
	struct usb_host_endpoint *ep = usb_pipe_endpoint(usbdev, pipe);
	struct urb *urb = usb_alloc_urb(0, mem_flags);

	if (unlikely(!urb)) {
		return NULL;
	}

	/* Allocate URB buffer */
	urb->transfer_buffer =
		usb_alloc_coherent(
			usbdev,
			ep->desc.wMaxPacketSize,
			mem_flags,
			&(urb->transfer_dma)
		);
		
	if (unlikely(!urb->transfer_buffer)) {
		goto fail;
	}
	
	urb->dev = usbdev;
	urb->pipe = pipe;
	urb->transfer_buffer_length = ep->desc.wMaxPacketSize;
	urb->complete = callback;
	urb->interval = ep->desc.bInterval;
	urb->start_frame = -1;
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	return urb;

fail:
	usb_free_urb(urb);
	
	return NULL;
}

void xpad360c_destroy_urb(struct urb *urb)
{
	struct usb_host_endpoint *ep = usb_pipe_endpoint(urb->dev, urb->pipe);

	usb_poison_urb(urb);

	usb_free_coherent(
		urb->dev,
		ep->desc.wMaxPacketSize,
		urb->transfer_buffer,
		urb->transfer_dma
	);

	usb_free_urb(urb);
}



/* Callers must do the following:
   	controller *must* be allocated. 
   	They *must* set controller->in->complete themselves.
   	They must *not* allocate anything else within the xpad360_controller struct.
	If the return value is not zero, they must disown the interface.

   It's suggested you call this function last, with the exception of setting
   the urb complete member for controller->in.

   You must also register anything yourself. This, unfortunately, cannot be abstracted well. 
*/
int xpad360c_allocate(struct xpad360_controller *controller, struct usb_interface *interface)
{
	struct usb_device * usbdev = interface_to_usbdev(interface);
	struct usb_endpoint_descriptor *ep_out = &(interface->cur_altsetting->endpoint[1].desc);
	struct usb_endpoint_descriptor *ep_in = &(interface->cur_altsetting->endpoint[0].desc);
	int error = 0;

	/* Initialize common urbs */
	controller->out = 
	xpad360c_allocate_urb(
		usbdev,
		usb_sndintpipe(usbdev, ep_out->bEndpointAddress),
		xpad360c_complete, GFP_KERNEL
	);
	
	if (unlikely(!controller->out)){
		goto fail0;
	}

	controller->in = 
	xpad360c_allocate_urb(
		usbdev,
		usb_rcvintpipe(usbdev, ep_in->bEndpointAddress),
		xpad360c_complete, GFP_KERNEL
	);
		
	if (unlikely(!controller->in)){
		goto fail1;
	}

	goto success;

fail1:
	xpad360c_destroy_urb(controller->out);

fail0:
	kfree(controller);

success:
	return error;
}

/* Callers must at least do the following:
 	They must *not* deallocate controller->in. 
 	They must *not* deallocate controller->out. 

   It's suggested you call this function first. 
 */
void xpad360c_destroy(struct xpad360_controller *controller)
{
	xpad360c_destroy_urb(controller->in);
	xpad360c_destroy_urb(controller->out);
}