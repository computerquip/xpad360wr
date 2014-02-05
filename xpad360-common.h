#pragma once

#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb/input.h>

/* Careful, is meant to be called in a void function.  */
#define CHECK_URB_STATUS(urb) \
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

enum {
	XPAD360_EP_OUT,
	XPAD360_EP_IN
};
	
enum {
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

struct xpad360_request {
	dma_addr_t dma;
	void *buffer;
	struct urb *urb;
};

/* After a bit more research, it turns out the mutex isn't needed
   Since I've taken the time to implement it though, I'm going to keep them there for now. 
   Think of it like... my training wheels. */
struct xpad360_input {
	struct input_dev *dev;
	struct mutex mutex;
};

struct xpad360_controller;

struct input_work {
	struct work_struct work;
	struct xpad360_request *request;
	struct xpad360_input *input;
	struct usb_interface *usbintf;
};

struct xpad360_controller {
	/* Because of these hold their own controller struct, we have to have one per controller... */
	struct input_work register_input;
	struct input_work unregister_input;
	struct input_work process_input; /* Only work struct that takes advantage of the request member. */
	
	bool okay; /* You're not looking so well... are you okay? */
	uint8_t num_controller;
	
	struct xpad360_request out_presence;

	/* All of the below is common between all controllers */
	struct xpad360_input input;
	struct usb_interface *usbintf;

	/* On wireless devices, 'in' is reset on
	 * every single input event. This allows to move input
	 * parsing outside of interrupt context and allows the use
	 * of mutex at the cost of more memory allocation... not sure
	 * which one is worse yet. */
	struct xpad360_request *in;
	
	/* Instead of setting up syncronization, we just allocate fresh resources per controller. */
	struct xpad360_request out_led;
	struct xpad360_request out_rumble;

	char path[64]; /* Physical stable path we can reference to */
};

int xpad360_common_init_request(
	struct xpad360_request *request, 
	struct usb_interface *intf, 
	int direction, 
	void(*callback)(struct urb*), /* May be NULL for generic handling if direction is XPAD360_EP_OUT */
	gfp_t mem_flags
);

void xpad360_common_destroy_request(
	struct xpad360_request *request, 
	struct usb_interface *intf,
	int direction
);

void xpad360_common_complete(struct urb *urb);
void xpad360_common_parse_input(struct input_dev *inputdev, void *_data);
void xpad360_common_init_input_dev(struct input_dev *inputdev, struct usb_interface *intf);