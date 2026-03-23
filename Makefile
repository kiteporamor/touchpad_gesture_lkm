obj-m += touchpad_gesture_lkm.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	sudo insmod touchpad_gesture_lkm.ko

uninstall:
	sudo rmmod touchpad_gesture_lkm

debug:
	sudo dmesg | grep -i "gesture\|touchpad"

info:
	sudo dmesg | tail -20

test-probe:
	echo "Check if probe was called:"
	sudo dmesg | grep -i "probe"

test-remove:
	sudo rmmod touchpad_gesture_lkm && sudo dmesg | grep -i "remove"

load-manual:
	sudo modprobe input_core && sudo insmod touchpad_gesture_lkm.ko

device-check:
	ls -la /sys/bus/platform/devices/ | grep touchpad
	ls -la /sys/bus/platform/drivers/ | grep touchpad