CROSS_COMPILE=arm-linux-gnueabihf-
CC:=$(CROSS_COMPILE)gcc
CPP:=$(CC) -E
LD:=$(CROSS_COMPILE)ld
AS:=$(CROSS_COMPILE)as
AR:=$(CROSS_COMPILE)ar
NM:=$(CROSS_COMPILE)nm
OBJDUMP:=$(CROSS_COMPILE)objdump
OBJCOPY:=$(CROSS_COMPILE)ojbcopy

export CC LD AS NM OBJDUMP OBJCOPY

TOPDIR:=$(shell pwd)
CFLAGS:=-Wall -Werror -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-maybe-uninitialized -g -O2 -I$(TOPDIR)/include -I/home/luo/linux/lib/libpng/include -I/home/luo/linux/lib/libjpeg/include -I/home/luo/linux/lib/freetype/include -I/home/luo/linux/lib/tslib/include -I/home/luo/linux/lib/giflib/include
LDFLAGS:= -lpthread -lpng -L/home/luo/linux/lib/libpng/lib -lz -L/home/luo/linux/lib/zlib/lib -ljpeg -L/home/luo/linux/lib/libjpeg/lib -lfreetype -L/home/luo/linux/lib/freetype/lib -lts -L/home/luo/linux/lib/tslib/lib -lgif -L/home/luo/linux/lib/giflib/lib
 
export TOPDIR CFLAGS LDFLAGS

TARGET=main

obj-y+=main.o
obj-y+=debug/
obj-y+=display/
obj-y+=decoder/
obj-y+=render/
obj-y+=page/
obj-y+=input/
obj-y+=file/

all:
	$(MAKE) -C ./ -f $(TOPDIR)/Makefile.build
	$(CC) $(LDFLAGS) -o $(TARGET) built-in.o
	sudo cp main /home/luo/mnt/nfs/rootfs/home/luo/app
clean:
	rm -f $(shell find -name "*.o")
	rm -f $(TARGET)

distclean:
	rm -f $(shell find -name "*.o")
	rm -f $(shell find -name "*.d")
	rm -f $(TARGET)

.PHONY:all clean distclean
