obj-m := v4l2loopback-dc.o

all:
	make -C /lib/modules/`uname -r`/build M=`pwd`

test:
	gcc test.c -o test

clean:
	make -C /lib/modules/`uname -r`/build M=`pwd` clean

insmod:
	sudo insmod v4l2loopback-dc.ko width=320 height=240

rmmod:
	sudo rmmod v4l2loopback-dc.ko
