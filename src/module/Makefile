KDIR = /lib/modules/$(shell uname -r)/build
obj-m := ums_mod.o
ums_mod-y := ums_scheduler.o ums_device.o ums_complist.o
all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
