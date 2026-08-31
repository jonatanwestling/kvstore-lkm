obj-m += kvstore.o
kvstore-objs := kvlkm.o kv_hash.o

ccflags-y += -I$(src)/../include

all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
