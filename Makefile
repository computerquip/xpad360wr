CONFIG_JOYSTICK_XPAD360W  ?= m
CONFIG_JOYSTICK_XPAD360WR ?= m

obj-$(CONFIG_JOYSTICK_XPAD360W) += xpad360w.o
obj-$(CONFIG_JOYSTICK_XPAD360WR) += xpad360wr.o

xpad360wr-y := xpad360c.o xpad360wr_usb.o
xpad360w-y  := xpad360c.o xpad360w_usb.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

install:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules_install

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
