# Arduino Project

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
arduino-cli monitor -p /dev/ttyUSB0 -b arduino:avr:uno --config 115200
```

```
Monitor port settings:
  baudrate=115200
  bits=8
  dtr=on
  parity=none
  rts=on
  stop_bits=1

Connecting to /dev/ttyUSB0. Press CTRL-C to exit.
Running Blink project...
```
