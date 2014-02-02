#pragma once

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb/input.h>

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

/* Wireless controllers use protocol 129 
 * Wired controllers use protocol 1
 * This should be the same across all Xbox 360 controllers.
 * Other interfaces with different protocols are not controllers. 
 */


struct xpad360_request {
	dma_addr_t dma;
	void *buffer;
	struct urb *urb;
};

struct xpad360_controller {
	bool present;
	uint8_t num_controller;

	struct input_dev *inputdev;
	struct usb_interface *usbintf;

	struct xpad360_request in;
	struct xpad360_request out;

	char path[64]; /* Physical stable path we can reference to */
};

void xpad360_common_parse_input(struct xpad360_controller *controller, void *_data);