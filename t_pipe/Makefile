ifneq ($(KERNELRELEASE),)
# kbuild part of makefile
obj-m  := t_pipe.o
CFLAGS_t_pipe.o := -DDEBUG

else
# normal makefile
KDIR ?= /lib/modules/`uname -r`/build

default:
	$(MAKE) -C $(KDIR) M=$$PWD

clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean

install:
	$(MAKE) -C $(KDIR) M=$$PWD modules_install

start:
	sudo insmod t_pipe.ko

stop:
	sudo rmmod t_pipe

test:
	echo "hello" > /dev/t_pipe
	cat /dev/t_pipe

endif
