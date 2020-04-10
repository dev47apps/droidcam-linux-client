obj-m := v4l2loopback-dc.o
KVERSION := $(shell uname -r)
RES_WIDTH := 640
RES_HEIGHT := 480

all:
	$(MAKE) -C /lib/modules/$(KVERSION)/build M=$(PWD)

test:
	gcc test.c -o test

clean:
	$(MAKE) -C /lib/modules/$(KVERSION)/build M=$(PWD) clean

insmod:
	sudo insmod v4l2loopback-dc.ko width=$(RES_WIDTH) height=$(RES_HEIGHT)

rmmod:
	sudo rmmod v4l2loopback-dc.ko
