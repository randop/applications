#!/bin/sh
# Configure the Luckfox Pico's USB OTG controller as a CTAPHID-compliant
# USB HID gadget (the same low-level transport real USB security keys use).
#
# Prereqs (check with: zcat /proc/config.gz 2>/dev/null | grep CONFIGFS ,
# or just try running this — it'll fail loudly if missing):
#   CONFIG_USB_CONFIGFS=y
#   CONFIG_USB_CONFIGFS_F_HID=y
#   CONFIG_USB_GADGET=y
# Luckfox's default Buildroot config usually has these; if not, add them in
# the kernel config used by the Luckfox SDK and rebuild.

set -e

GADGET=/sys/kernel/config/usb_gadget/fidokey

if [ -d "$GADGET" ]; then
    echo "Gadget already configured, skipping"
    exit 0
fi

mkdir -p "$GADGET"
cd "$GADGET"

# NOTE: 0x1209/0x0001 is a placeholder from the pid.codes open-source shared
# VID pool — fine for bench/personal use. For anything you'd distribute,
# request your own PID at https://pid.codes.
echo 0x1209 > idVendor
echo 0x0001 > idProduct
echo 0x0100 > bcdDevice
echo 0x0200 > bcdUSB

mkdir -p strings/0x409
echo "diy"              > strings/0x409/manufacturer
echo "Luckfox FIDO Key" > strings/0x409/product
echo "0001"             > strings/0x409/serialnumber

mkdir -p configs/c.1/strings/0x409
echo "FIDO HID config" > configs/c.1/strings/0x409/configuration
echo 120 > configs/c.1/MaxPower

mkdir -p functions/hid.usb0
echo 0  > functions/hid.usb0/protocol
echo 0  > functions/hid.usb0/subclass
echo 64 > functions/hid.usb0/report_length

# Standard FIDO/U2F HID report descriptor (64-byte in/out reports,
# FIDO_USAGE_PAGE 0xF1D0, usage 0x01) — matches every USB CTAPHID device.
printf '\006\320\361\011\001\241\001\011\040\025\000\046\377\000\165\010\225\100\201\002\011\041\025\000\046\377\000\165\010\225\100\221\002\300' \
    > functions/hid.usb0/report_desc

ln -s functions/hid.usb0 configs/c.1/

UDC=$(ls /sys/class/udc | head -n1)
if [ -z "$UDC" ]; then
    echo "No UDC found — is the OTG controller in device mode? Check the" >&2
    echo "board's dr_mode setting (device tree) and USB cable/port used." >&2
    exit 1
fi
echo "$UDC" > UDC

echo "Gadget bound to UDC: $UDC"
echo "Look for /dev/hidg0 now."
