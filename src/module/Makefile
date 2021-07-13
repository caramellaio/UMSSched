KDIR = /lib/modules/$(shell uname -r)/build
obj-m += user_mode_sched.o

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
