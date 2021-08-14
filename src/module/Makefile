KDIR = /lib/modules/$(shell uname -r)/build
obj-m += ums_device.o

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
