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
struct input_work {
	struct work_struct work;
	struct xpad360_controller *controller;
};

struct xpad360_controller {
	struct input_work register_input;
	struct input_work unregister_input;

	bool present;
	uint8_t num_controller;

	struct input_dev *inputdev;
	struct usb_interface *usbintf;

	struct xpad360_request in;
	struct xpad360_request out;

	char path[64]; /* Physical stable path we can reference to */
};

void xpad360_common_parse_input(struct xpad360_controller *controller, void *_data);