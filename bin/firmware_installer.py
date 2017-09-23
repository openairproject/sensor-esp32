1. ask for UART NAME
2.
make TEMP_DIR
git clone https://github.com/espressif/esptool.git -o TEMP_DIR
3.
TEMP_DIR/esptool.py --port /dev/tty.SLAB_USBtoUART --after no_reset chip_id
4.
fetch https://openairproject.com/ota/index.txt to TEMP_DIR
parse first line
fetch binaries to TEMP_DIR
test sha
5.
fetch partitions_two_ota.bin
fetch bootloader.bin
6.
python TEMP_DIR/esptool.py --chip esp32 --port /dev/tty.SLAB_USBtoUART --baud 921600 --before default_reset 
		--after hard_reset write_flash -u --flash_mode dio --flash_freq 40m --flash_size detect
		0x1000 TEMP_DIR/bootloader.bin 0x10000 TEMP_DIR/sensor-esp32.bin 0x8000 TEMP_DIR/partitions_two_ota.bin