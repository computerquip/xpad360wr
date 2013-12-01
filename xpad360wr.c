#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb/input.h>

MODULE_AUTHOR("Zachary Lund <admin@computerquip.com>");
MODULE_DESCRIPTION("Xbox 360 Wireless Adapter");
MODULE_LICENSE("GPL");

/* These are constant values concerning packets */
#define MAX_PACKET_SIZE 32

/*
LED status definitions (in the form of an enum):
	OFF = 0x00,
	BLINKING,
	FLASH_1_ON,
	FLASH_2_ON,
	FLASH_3_ON,
	FLASH_4_ON,
	ON_1,
	ON_2,
	ON_3,
	ON_4,
	ROTATING,
	SECTIONAL_BLINKING,
	SLOW_SECTIONAL_BLINKING,
	ALTERNATING
*/

/* Some Got'chyas:
	The controller is smart.
	It turns off after a certain amount of time.
	It automatically sets LED status after unplugging receiver.
	However, the driver is required to set led status after plugging it back in.
	The receiver manages what controller is on what interface... we do not.
	It does not matter when the controller is disconnected, we let the receiver handle the LED status on that.
	The xpad driver incorrectly parses various packets, I used it more as a reference for driver structure.
	There is no need for a mutex as the driver is designed to have no possible data races (tell me if you find one please).
	It is near impossible to set LED status before module completely unloads. I've spent hours trying to fix this...
	There is no way to determine whether a controller is already connected on module load that we know of... perhaps somewhere in the packets.

   TODO:
	I need to figure out what the rest of these damn packets do...
*/
struct xpad360wr_buffer {
    dma_addr_t dma; /*  */
    void *buffer;
};

struct xpad360wr_headset {
    /* Nothing, I don't have one to test with. */
};

struct xpad360wr_controller {
    u8 num_controller;

    struct input_dev *inputdev; /* input subsystem device */
    struct usb_interface *usbintf; /* usb subsystem interface */
    struct usb_device *usbdev; /* usb subsystem device... perhaps not needed since can be obtained from interface */

    bool present;

    struct xpad360wr_buffer ep_in;
    struct xpad360wr_buffer ep_out;

    struct xpad360wr_headset *headset; /* Currently nothing since I don't have a headset */

    struct urb *irq_in;
    struct urb *irq_out;

    char path[64]; /* Physical stable path we can reference to */
};

static void xpad360wr_controller_set_led(struct xpad360wr_controller *controller, int status)
{
    u8 *data = controller->ep_out.buffer;

    /* Verbatim xboxdrv. */
    data[0] = 0x00;
    data[1] = 0x00;
    data[2] = 0x08;
    data[3] = (u8)(0x40 + (status % 0x0e));
    data[4] = 0x00;
    data[5] = 0x00;
    data[6] = 0x00;
    data[7] = 0x00;
    data[8] = 0x00;
    data[9] = 0x00;

    controller->irq_out->transfer_buffer_length = 10;
    usb_submit_urb(controller->irq_out, GFP_ATOMIC);
}

static int xpad360wr_controller_open(struct input_dev* dev)
{
    struct xpad360wr_controller *controller = input_get_drvdata(dev);
    printk("Controller #%i has been opened.\n", controller->num_controller);
    if (controller->present == false) {/* Not actually there, despite being properly probed and available */
        printk("Controller isn't present, returning -1.\n");
        return -1;
    }

    return 0;
}

static void xpad360wr_controller_close(struct input_dev* dev)
{
    struct xpad360wr_controller *controller = input_get_drvdata(dev);
    printk("Controller #%i has been closed.\n", controller->num_controller);
}

static void xpad360wr_irq_send(struct urb *urb)
{
    struct xpad360wr_controller *controller = urb->context;

    switch (urb->status) {
    case 0:
        printk("Controller #%i sent message successfully!\n", controller->num_controller);
        return;
    case -ECONNRESET:
        printk("Controller #%i was reset.\n", controller->num_controller);
        return;
    case -ESHUTDOWN:
        printk("Controller #%i has shutdown.\n", controller->num_controller);
        return;
    default:
        printk("Unknown status returned by controller #%i.\n", controller->num_controller);
        return;
    }
}

static void xpad360wr_irq_receive(struct urb *urb)
{
    struct xpad360wr_controller *controller = urb->context;
    struct input_dev *dev = controller->inputdev;
    unsigned char* data = controller->ep_in.buffer;

    switch (urb->status) {
    case 0:
        break;
    case -ECONNRESET:
        printk("Controller #%i was reset.\n", controller->num_controller);
        return;
    case -ESHUTDOWN:
        printk("Controller #%i has shutdown.\n", controller->num_controller);
        return;
    default:
        printk("Unknown status returned by controller #%i.\n", controller->num_controller);
        return;
    }

    /* 	At this point, we receive a valid packet with normal status.
    	We parse the packet and push into corresponding events.
    	This is the most difficult part since it's all reverse engineered.
    	References for this code go to original xpad driver and xboxdrv, the userspace driver.
    	I found other online references incorrect so I have not used them and will not be listed.
     */

    /*
     * Notes on header integer
     *	The header array may not be converted to an integer correctly, but that's not really the goal.
     *  This method allows us to follow the code from xboxdrv and other documents where viable.
     *  It's platform independent. It helps readability with USB packet sniffers and in code as well.
     */

    /* Event from Wireless Receiver */
    if (urb->actual_length == 2) {
        u16 header = le16_to_cpu((data[0] << 8) | data[1]);

        switch (header) {
            /* Controller disconnected */
        case 0x0800:
            if (controller->present == true) { /* This can be false if a device is still connected to receiver before and after module load up */
                controller->present = false;
                printk("Controller #%i has disconnected!\n", controller->num_controller);
            }
            break;
            /* Controller connected */
        case 0x0880:
            xpad360wr_controller_set_led(controller, controller->num_controller + 2);
            controller->present = true;
            printk("Controller #%i has connected!\n", controller->num_controller);
            break;
            /* Headset connected */
        case 0x0840:
            printk("Controller #%i has connected a headset!\n", controller->num_controller);
            break;
            /* Controller with headset connect */
        case 0x08C0:
            controller->present = true;
            printk("Controller #%i has connected with a headset!\n", controller->num_controller);
            break;
        default:
            printk("Unknown packet received. Length was 2, header was %#.4x\n", header);
        }
    }
    /* Event from Controller */
    else if (urb->actual_length == 29) {
        u32 header = le32_to_cpu((data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3]);

        switch (header) {
            /* Announcment */
        case 0x000F00F0:
            printk(
                "Serial: %i:%i:%i:%i:%i:%i:%i\n",
                data[7], data[8], data[9], data[10], data[11], data[12], data[13]
            );
            printk("Battery Status: %i\n", data[17]);
            break;

            /* Event report */
        case 0x000100F0:
            /* I'm not sure what data[4] and data[5]*/

            /* Mostly from xpad driver */
            input_report_key(dev, BTN_TRIGGER_HAPPY3, data[6] & 0x01); /* D-pad up	 */
            input_report_key(dev, BTN_TRIGGER_HAPPY4, data[6] & 0x02); /* D-pad down */
            input_report_key(dev, BTN_TRIGGER_HAPPY1, data[6] & 0x04); /* D-pad left */
            input_report_key(dev, BTN_TRIGGER_HAPPY2, data[6] & 0x08); /* D-pad right */

            /* start/back buttons */
            input_report_key(dev, BTN_START,  data[6] & 0x10);
            input_report_key(dev, BTN_SELECT, data[6] & 0x20); /* Back */

            /* stick press left/right */
            input_report_key(dev, BTN_THUMBL, data[6] & 0x40);
            input_report_key(dev, BTN_THUMBR, data[6] & 0x80);

            input_report_key(dev, BTN_TL,	data[7] & 0x01); /* Left Shoulder */
            input_report_key(dev, BTN_TR,	data[7] & 0x02); /* Right Shoulder */
            input_report_key(dev, BTN_MODE,	data[7] & 0x04); /* Guide */
            /* data[8] & 0x08 is a dummy value */
            input_report_key(dev, BTN_A,	data[7] & 0x10);
            input_report_key(dev, BTN_B,	data[7] & 0x20);
            input_report_key(dev, BTN_X,	data[7] & 0x40);
            input_report_key(dev, BTN_Y,	data[7] & 0x80);

            input_report_abs(dev, ABS_Z, data[8]);
            input_report_abs(dev, ABS_RZ, data[9]);

            /* left stick */
            input_report_abs(dev, ABS_X, (s16)le16_to_cpup((u16*)&data[10]));
            input_report_abs(dev, ABS_Y, (s16)le16_to_cpup((u16*)&data[12]));

            /* right stick */
            input_report_abs(dev, ABS_RX, (s16)le16_to_cpup((u16*)&data[14]));
            input_report_abs(dev, ABS_RY, (s16)le16_to_cpup((u16*)&data[16]));

            break;

            /*
             * This isn't used in xboxdrv.
             * It's sent after every button press.
             * It's sent after a certain amount of time if not a button press.
             * If not a button press and a lot of events have passed, there appears to be a maximum amount allowed before this is sent.
             */
        case 0x000000F0:
            input_sync(dev);
            break;

        default:
            printk("Unknown packet receieved. Length was 29, header was %#.8x\n", header);
        }
    }
    /* No known case of this happening. */
    else {
        printk("Unknown packet received. Length was %i... what about the header...?", urb->actual_length);
    }

    usb_submit_urb(urb, GFP_ATOMIC);
}

int xpad360wr_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
    struct usb_device * usbdev = interface_to_usbdev(interface);
    struct usb_endpoint_descriptor *usbep = &(interface->cur_altsetting->endpoint[0].desc);
    struct xpad360wr_controller *controller = kzalloc(sizeof(struct xpad360wr_controller), GFP_KERNEL);
    int error = 0;

    controller->present = false;
    controller->usbdev = usbdev;
    controller->usbintf = interface;

    {
        const u8 num_interface = interface->cur_altsetting->desc.bInterfaceNumber;

        printk("Probing interface #%i\n", num_interface);


        if (num_interface % 2 == 1)
            return -1;

        controller->num_controller = (num_interface + 1) / 2;
    }

    controller->inputdev = input_allocate_device();
    if (controller->inputdev == NULL) {
        error = -ENOMEM;
        goto fail0;
    }

    /* Initialize in endpoint */
    controller->ep_in.buffer =
        usb_alloc_coherent(
            usbdev,
            MAX_PACKET_SIZE,
            GFP_KERNEL,
            &(controller->ep_in.dma)
        );

    if (!controller->ep_in.buffer) {
        error = -ENOMEM;
        goto fail0;
    }

    controller->irq_in = usb_alloc_urb(0, GFP_KERNEL);

    if (!controller->irq_in) {
        error = -ENOMEM;
        goto fail1;
    }

    {
        char tmp[8];
        snprintf(tmp, sizeof(tmp), "/input%i", controller->num_controller);
        usb_make_path(usbdev, controller->path, sizeof(controller->path));
        strlcat(controller->path, tmp, sizeof(controller->path));
    }

    /* Initialize input device */
    controller->inputdev->name = "Xbox 360 Wireless Receiver"; /* HARD CODED, OMG, FIX */
    controller->inputdev->phys = controller->path; /* Probably more issues here... */
    controller->inputdev->dev.parent = &(interface->dev);
    controller->inputdev->open = xpad360wr_controller_open;
    controller->inputdev->close = xpad360wr_controller_close;

    usb_to_input_id(usbdev, &controller->inputdev->id);

    input_set_drvdata(controller->inputdev, controller);
    usb_set_intfdata(interface, controller);

    usb_fill_int_urb(
        controller->irq_in, usbdev,
        usb_rcvintpipe(usbdev, usbep->bEndpointAddress),
        controller->ep_in.buffer, MAX_PACKET_SIZE, xpad360wr_irq_receive,
        controller, usbep->bInterval /* Needs encoding which is why I don't just use 1 */
    );

    controller->irq_in->transfer_dma
        = controller->ep_in.dma;
    controller->irq_in->transfer_flags
    |= URB_NO_TRANSFER_DMA_MAP;

    /* Initialize out endpoint */
    usbep = &(interface->cur_altsetting->endpoint[1].desc);

    controller->ep_out.buffer =
        usb_alloc_coherent(
            usbdev,
            MAX_PACKET_SIZE,
            GFP_KERNEL,
            &(controller->ep_out.dma)
        );

    if (!controller->ep_out.buffer) {
        error = -ENOMEM;
        goto fail2;
    }

    controller->irq_out = usb_alloc_urb(0, GFP_KERNEL);

    if (!controller->irq_out) {
        error = -ENOMEM;
        goto fail3;
    }

    usb_fill_int_urb(
        controller->irq_out, usbdev,
        usb_sndintpipe(usbdev, usbep->bEndpointAddress),
        controller->ep_out.buffer, MAX_PACKET_SIZE, xpad360wr_irq_send,
        controller, usbep->bInterval
    );

    controller->irq_out->transfer_dma = controller->ep_out.dma;
    controller->irq_out->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

    /* Populate device capabilities */
#define SET_BIT(type) __set_bit(type, controller->inputdev->keybit);

    controller->inputdev->evbit[0] = BIT_MASK(EV_KEY); /* General device that has key presses. */
    SET_BIT(BTN_A);
    SET_BIT(BTN_B);
    SET_BIT(BTN_X);
    SET_BIT(BTN_Y);
    SET_BIT(BTN_START);
    SET_BIT(BTN_SELECT);
    SET_BIT(BTN_THUMBL);
    SET_BIT(BTN_THUMBR);
    SET_BIT(BTN_TRIGGER_HAPPY1);
    SET_BIT(BTN_TRIGGER_HAPPY2);
    SET_BIT(BTN_TRIGGER_HAPPY3);
    SET_BIT(BTN_TRIGGER_HAPPY4);
    SET_BIT(BTN_TL);
    SET_BIT(BTN_TR);
    SET_BIT(BTN_MODE);

#undef SET_BIT
#define SET_BIT(type) \
	__set_bit(type, controller->inputdev->absbit);\
	input_set_abs_params(controller->inputdev, type, -32768, 32767, 16, 128);

    /* Axis... */
    controller->inputdev->evbit[0] |= BIT_MASK(EV_ABS);

    SET_BIT(ABS_X);
    SET_BIT(ABS_Y);
    SET_BIT(ABS_RX);
    SET_BIT(ABS_RY);

#undef SET_BIT
#define SET_BIT(type) \
	__set_bit(type, controller->inputdev->absbit); \
	input_set_abs_params(controller->inputdev, type, 0, 255, 0, 0);

    /* Triggers... */
    SET_BIT(ABS_Z);
    SET_BIT(ABS_RZ);

#undef SET_BIT

    error = input_register_device(controller->inputdev);
    if (error) {
        printk("input_register_device() failed!");
        goto fail4;
    }

    controller->irq_in->dev = usbdev;
    error = usb_submit_urb(controller->irq_in, GFP_KERNEL);
    if (error != 0) {
        printk("usb_submit_urb(controller->irq_in) failed!\n");
        goto fail5;
    }

    return 0;

fail5:
    usb_free_urb(controller->irq_out);
fail4:
    input_free_device(controller->inputdev);
fail3:
    usb_free_coherent(
        usbdev,
        MAX_PACKET_SIZE,
        controller->ep_out.buffer,
        controller->ep_out.dma
    );
fail2:
    usb_free_urb(controller->irq_in);
fail1:
    usb_free_coherent(
        usbdev,
        MAX_PACKET_SIZE,
        controller->ep_in.buffer,
        controller->ep_in.dma
    );

fail0:
    return error;
    ;
}

void xpad360wr_disconnect(struct usb_interface* interface)
{
    struct usb_device * usbdev = interface_to_usbdev(interface);
    struct xpad360wr_controller *controller = usb_get_intfdata(interface);

    printk("Controller #%i disconnected.\n", controller->num_controller);

    input_unregister_device(controller->inputdev);

    usb_free_coherent(
        usbdev,
        MAX_PACKET_SIZE,
        controller->ep_in.buffer,
        controller->ep_in.dma
    );

    usb_free_coherent(
        usbdev,
        MAX_PACKET_SIZE,
        controller->ep_out.buffer,
        controller->ep_out.dma
    );

    usb_free_urb(controller->irq_in);
    usb_free_urb(controller->irq_out);

    kfree(controller);
    usb_set_intfdata(interface, NULL);
}

static struct usb_device_id xpad360wr_table[] = {
    { USB_DEVICE(0x045e /* Microsoft */, 0x0719)},
    {}
};

static struct usb_driver xpad360wr_driver = {
    .name		= "xpad360wr",
    .probe		= xpad360wr_probe,
    .disconnect	= xpad360wr_disconnect,
    .id_table	= xpad360wr_table,
};

MODULE_DEVICE_TABLE(usb, xpad360wr_table);
module_usb_driver(xpad360wr_driver);