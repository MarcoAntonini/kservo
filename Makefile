obj-m := servo.o

KERNELDIR?= /lib/modules/$(shell uname -r)/build
PWD		:= $(shell pwd)

all default:
	$(MAKE) -C $(KERNELDIR) SUBDIRS=$(PWD) modules

clean:
	rm -f *.ko *.o *.symvers *.mod.c
