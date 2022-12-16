obj-m = kernelmodule.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
	gcc client.c -o client
	sudo insmod kernelmodule.ko

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
	sudo rmmod kernelmodule.ko
	rm client
