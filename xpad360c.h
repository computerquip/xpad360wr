#pragma once

#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb/input.h>

#define CHECK_URB_STATUS(device, urb) \
	switch (urb->status) { \
	case 0: \
		break; \
	case -ECONNRESET: \
		dev_dbg(device, "Controller has been reset.\n");\
		return; \
	case -ESHUTDOWN: \
		dev_dbg(device, "Controller has shutdown.\n"); \
		return; \
	case -ENOENT: \
		dev_dbg(device, "Controller has been poisoned.\n"); \
		return; \
	default: \
		dev_dbg(device, "Unknown status returned by controller: %x\n", urb->status); \
		return; \
	} 
	
enum xpad360c_led_t{
	XPAD360_LED_OFF,
	XPAD360_LED_ALL_BLINKING,
	XPAD360_LED_FLASH_ON_1,
	XPAD360_LED_FLASH_ON_2,
	XPAD360_LED_FLASH_ON_3,
	XPAD360_LED_FLASH_ON_4,
	XPAD360_LED_ON_1,
	XPAD360_LED_ON_2,
	XPAD360_LED_ON_3,
	XPAD360_LED_ON_4,
	XPAD360_LED_ROTATING,
	XPAD360_LED_SECTIONAL_BLINKING,
	XPAD360_LED_SLOW_SECTIONAL_BLINKING,
	XPAD360_LED_ALTERNATING
};

struct packet_work {
	struct work_struct work;
	struct urb *urb;
	struct input_dev *inputdev;
};

/* Our main structure. 
   Only oddball here is the out urb. 
   It's implicitly readonly after initialization. 
 */
struct xpad360_controller {

	struct input_dev *inputdev;

	struct urb *in;
	struct urb *out;

	const char* name; /* A human-readable name for the controller, used for input device structure. */
	char path[64];
};

int xpad360c_allocate(
	struct xpad360_controller *controller,
	struct usb_interface *interface
);

void xpad360c_destroy(struct xpad360_controller* controller);

struct urb* xpad360c_allocate_urb(
	struct usb_device *usbdev,
	int pipe,
	void(*callback)(struct urb*),
	gfp_t mem_flags
);

void xpad360c_destroy_urb(struct urb *urb);

static inline 
struct urb* xpad360c_copy_urb(struct urb *old_urb, gfp_t mem_flags)
{
	struct urb* urb =
	xpad360c_allocate_urb(old_urb->dev, old_urb->pipe, old_urb->complete, mem_flags);

	urb->context = old_urb->context;
	return urb;
}

void xpad360c_complete(struct urb *urb); /* Generic handler that checks for status, gives a message on error, then returns. */
void xpad360c_parse_input(struct input_dev *inputdev, void *_data);

void xpad360c_allocate_inputdev(struct xpad360_controller *controller);
void xpad360c_destroy_inputdev(struct xpad360_controller *controller);