#pragma once

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

struct xpad360_controller;

/* This is mostly for wireless devices to dynamically register input */

struct xpad360_controller {
	bool present;
	uint8_t num_controller;

	struct input_dev *inputdev;
	struct usb_interface *usbintf;

	struct xpad360_request in;
	
	/* Instead of setting up syncronization, we just allocate fresh resources per controller. */
	struct xpad360_request out_led;
	struct xpad360_request out_rumble;
	struct xpad360_request out_presence;

	char path[64]; /* Physical stable path we can reference to */
};

int xpad360_common_init_request(
	struct xpad360_request *request, 
	struct usb_interface *intf, 
	int direction, 
	void(*callback)(struct urb*) /* May be NULL for generic handling */
);

void xpad360_common_destroy_request(
	struct xpad360_request *request, 
	struct usb_interface *intf,
	int direction
);

void xpad360_common_complete(struct urb *urb);
void xpad360_common_parse_input(struct xpad360_controller *controller, void *_data);
void xpad360_common_init_input_dev(struct input_dev *inputdev, struct xpad360_controller *controller);