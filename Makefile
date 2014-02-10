CONFIG_JOYSTICK_XPAD360 ?= m
obj-$(CONFIG_JOYSTICK_XPAD360) += xpad360.o

xpad360-objs := xpad360-common.o xpad360wr.o xpad360w.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
