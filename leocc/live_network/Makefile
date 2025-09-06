ifneq ($(KERNELRELEASE),)
	obj-m:=leocc.o
else
	KDIR:=/lib/modules/$(shell uname -r)/build
all:
	make -C $(KDIR) M=$(PWD) modules
clean:
	rm -f *.ko *.o *.mod.o *.mod.c *.symvers *.order *.mod
endif
