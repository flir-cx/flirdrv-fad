
# typically use the following to compile
# make ARCH=arm CROSS_COMPILE=/home/fredrik/mentor/arm-2011.03/bin/arm-none-linux-gnueabi
#
# Also modify 'KERNELDIR' to fit your system

EXTRA_CFLAGS = -I$(ALPHAREL)/SDK/FLIR/Include

	obj-m := fad.o
	fad-objs += faddev.o
	fad-objs += fad_io.o
	fad-objs += fad_irq.o
	fad-objs += bspfaddev.o
	KERNELDIR ?= /home/pfitger/linux-2.6-imx
	PWD := $(shell pwd)
default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

