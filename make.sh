#!/bin/bash -e
make COMPILE=gcc BOOT=none APP=0 SPI_SPEED=40 SPI_MODE=DIO SPI_SIZE_MAP=0
esptool.py --port /dev/ttyUSB0 --baud $((115200*2)) write_flash --flash_mode dio --flash_size 32m 0x0 \
			../bin/eagle.flash.bin 0x40000 ../bin/eagle.irom0text.bin