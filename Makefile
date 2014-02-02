obj-m += xpad360-all.o

xpad360-all-y := xpad360-common.o xpad360wr.o xpad360.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
