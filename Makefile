
# typically use the following to compile
# make ARCH=arm CROSS_COMPILE=/home/fredrik/mentor/arm-2011.03/bin/arm-none-linux-gnueabi
#
# Also modify 'KERNELDIR' to fit your system

ifeq ($(KERNEL_SRC),)
	KERNEL_SRC ?= ~/linux/flir-yocto/build_pico/tmp-eglibc/work/neco-oe-linux-gnueabi/linux-boundary/3.0.35-r0/git
endif

ifneq ($(KERNEL_PATH),)
       KERNEL_SRC = $(KERNEL_PATH)
endif

EXTRA_CFLAGS = -I$(ALPHAREL)/SDK/FLIR/Include -Werror

	obj-m := fad.o
	fad-objs += faddev.o
	fad-objs += fad_irq.o
	fad-objs += fad_neco.o
	fad-objs += fad_roco.o
	fad-objs += fad_ninjago.o
	fad-objs += laser_pointer.o
	fad-objs += laser_distance.o
	PWD := $(shell pwd)

all: 
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean

