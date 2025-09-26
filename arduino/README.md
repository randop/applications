# Arduino Project

## Setup
`dmesg`
```
[ 9677.610085] usb 3-2: new full-speed USB device number 3 using xhci_hcd
[ 9677.750758] usb 3-2: New USB device found, idVendor=1a86, idProduct=7523, bcdDevice= 2.64
[ 9677.750768] usb 3-2: New USB device strings: Mfr=0, Product=2, SerialNumber=0
[ 9677.750773] usb 3-2: Product: USB Serial
[ 9677.796918] usbcore: registered new interface driver usbserial_generic
[ 9677.796933] usbserial: USB Serial support registered for generic
[ 9677.798802] usbcore: registered new interface driver ch341
[ 9677.798829] usbserial: USB Serial support registered for ch341-uart
[ 9677.798857] ch341 3-2:1.0: ch341-uart converter detected
[ 9677.811895] usb 3-2: ch341-uart converter now attached to ttyUSB0
```

`lsusb`
```
Bus 003 Device 003: ID 1a86:7523 QinHeng Electronics CH340 serial converter
```

```
# /etc/udev/rules.d/99-usb.rules
SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", SYMLINK+="ttyUSB1001", MODE="0666", GROUP="dialout"
```

```sh
udevadm control --reload-rules && udevadm trigger
```

```
# /etc/pve/lxc/103.conf 
lxc.cgroup2.devices.allow: c 188:* rwm
dev0: /dev/ttyUSB1001,mode=0666
```

## Installation
```sh
pacman -Syu base-devel meson avr-gcc avr-libc avrdude make
```

## Building

1. Configure: `meson setup build --cross-file avr-uno.txt`
2. Compile: `meson compile -C build`
3. Flash Arduino UNO: `ninja -C build upload`

#### Logs
```
ninja: Entering directory `build'
[0/1] Regenerating build files
DEPRECATION: c_args in the [properties] section of the machine file is deprecated, use the [built-in options] section.
DEPRECATION: cpp_args in the [properties] section of the machine file is deprecated, use the [built-in options] section.
DEPRECATION: c_link_args in the [properties] section of the machine file is deprecated, use the [built-in options] section.
DEPRECATION: cpp_link_args in the [properties] section of the machine file is deprecated, use the [built-in options] section.
The Meson build system
Version: 1.9.1
Source dir: /home/johnpaul/my_arduino_project
Build dir: /home/johnpaul/my_arduino_project/build
Build type: cross build
Project name: arduino-blink
Project version: 0.1
C compiler for the host machine: avr-gcc (gcc 15.1.0 "avr-gcc (GCC) 15.1.0")
C linker for the host machine: avr-gcc ld.bfd 2.43
C++ compiler for the host machine: avr-g++ (gcc 15.1.0 "avr-g++ (GCC) 15.1.0")
C++ linker for the host machine: avr-g++ ld.bfd 2.43
C compiler for the build machine: cc (gcc 15.2.1 "cc (GCC) 15.2.1 20250813")
C linker for the build machine: cc ld.bfd 2.45.0
C++ compiler for the build machine: c++ (gcc 15.2.1 "c++ (GCC) 15.2.1 20250813")
C++ linker for the build machine: c++ ld.bfd 2.45.0
Build machine cpu family: x86_64
Build machine cpu: x86_64
Host machine cpu family: avr
Host machine cpu: atmega328p
Target machine cpu family: avr
Target machine cpu: atmega328p
Program avr-objcopy found: YES (/usr/bin/avr-objcopy)
Program avr-size found: YES (/usr/bin/avr-size)
Program avrdude found: YES (/usr/bin/avrdude)
Build targets in project: 4

arduino-blink 0.1

  User defined options
    Cross files: /home/johnpaul/my_arduino_project/avr-uno.txt

Found ninja-1.12.1 at /usr/bin/ninja
Cleaning... 0 files.
[0/2] Running external command upload (wrapped by meson to set env)
Reading 1016 bytes for flash from input file blink.hex
Writing 1016 bytes to flash
Writing | ################################################## | 100% 0.18 s
1016 bytes of flash written
Avrdude done.  Thank you.
```

## Monitoring
```sh
arduino-cli monitor -p /dev/ttyUSB1001 -b arduino:avr:uno --config 115200
```

```
Monitor port settings:
  baudrate=115200
  bits=8
  dtr=on
  parity=none
  rts=on
  stop_bits=1

Connecting to /dev/ttyUSB1001. Press CTRL-C to exit.
Running arduino project...
```
