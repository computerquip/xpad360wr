obj-m := xpad360w.o xpad360wr.o

xpad360wr-y := xpad360wr_usb.o
xpad360w-y  := xpad360w_usb.o

ccflags-y   := -DDEBUG

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

install:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules_install

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
