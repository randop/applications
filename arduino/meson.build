project('arduino-project', ['c', 'cpp'],
  version: '0.1.5',
  default_options: [
    'buildtype=minsize',
    'default_library=static',
    'b_lto=false',
    'cpp_rtti=false',
    'cpp_eh=none',
    'c_std=gnu11',
    'cpp_std=gnu++11',
    'b_staticpic=false',
  ],
  meson_version: '>=1.0.0',
)

# Assert cross-build (Arduino requires it)
assert(meson.is_cross_build(), 'Arduino projects require cross-compilation.')

# Core sources (minimal for Blink: main() + wiring + digital I/O)
core_sources = [
  'ArduinoCore-avr/cores/arduino/main.cpp',
  'ArduinoCore-avr/cores/arduino/new.cpp',
  'ArduinoCore-avr/cores/arduino/abi.cpp',
  'ArduinoCore-avr/cores/arduino/HardwareSerial.cpp',
  'ArduinoCore-avr/cores/arduino/HardwareSerial0.cpp',
  'ArduinoCore-avr/cores/arduino/Print.cpp',
  'ArduinoCore-avr/cores/arduino/hooks.c',
  'ArduinoCore-avr/cores/arduino/wiring.c',
  'ArduinoCore-avr/cores/arduino/wiring_digital.c',
  'ArduinoCore-avr/cores/arduino/WMath.cpp',
  'ArduinoCore-avr/libraries/Wire/src/Wire.cpp',
  'ArduinoCore-avr/libraries/Wire/src/utility/twi.c',
  'ArduinoCore-avr/libraries/SPI/src/SPI.cpp',
  'ArduinoCore-avr/libraries/Adafruit_BusIO/Adafruit_GenericDevice.cpp',
  'ArduinoCore-avr/libraries/Adafruit_BusIO/Adafruit_I2CDevice.cpp',
  'ArduinoCore-avr/libraries/Adafruit_BusIO/Adafruit_SPIDevice.cpp',
  'ArduinoCore-avr/libraries/Ds1302/lib/Ds1302/Ds1302.cpp',
  'ArduinoCore-avr/libraries/Adafruit-GFX-Library/Adafruit_GFX.cpp',
  'ArduinoCore-avr/libraries/Adafruit_SSD1306/Adafruit_SSD1306.cpp',
  'ArduinoCore-avr/libraries/Arduino-I2CScanner/src/I2CScanner.cpp'
]

# source sketch
sketch_sources = ['src/main.cpp']

arduino_includes = include_directories('ArduinoCore-avr/cores/arduino') 
variant_standard_includes = include_directories('ArduinoCore-avr/variants/standard')
wire_include = include_directories('ArduinoCore-avr/libraries/Wire/src', 'ArduinoCore-avr/libraries/Wire/src/utility')
busio_include = include_directories('ArduinoCore-avr/libraries/Adafruit_BusIO')
rtclib_include = include_directories('ArduinoCore-avr/libraries/Ds1302/lib/Ds1302')
i2c_include = include_directories('ArduinoCore-avr/libraries/Arduino-I2CScanner/src')
spi_include = include_directories('ArduinoCore-avr/libraries/SPI/src')
gfx_include = include_directories('ArduinoCore-avr/libraries/Adafruit-GFX-Library', 'ArduinoCore-avr/libraries/Adafruit_SSD1306')

# Executable (ELF)
exe = executable('main',
  sources: core_sources + sketch_sources,
  name_suffix: 'elf',
  include_directories : [arduino_includes, variant_standard_includes, wire_include, i2c_include, spi_include, busio_include, rtclib_include, gfx_include]
)

# Generate HEX from ELF
objcopy = find_program('avr-objcopy')
hex_target = custom_target('main.hex',
  input: exe,
  output: 'main.hex',
  command: [objcopy, '-O', 'ihex', '-R', '.eeprom', '@INPUT@', '@OUTPUT@'],
  build_by_default: true,
  depends: exe,
)

# Extract HEX path for upload
hex_path = hex_target.full_path()

# Size info
size = find_program('avr-size')
run_target('size',
  command: [size, exe],
  depends: exe,
)

# Upload
avrdude = find_program('avrdude')
run_target('upload',
  command: [
    avrdude,
    '-F', '-V',
    '-c', 'arduino',
    '-p', 'atmega328p',
    '-P', get_option('port'),
    '-b', '115200',
    '-U', 'flash:w:' + hex_path + ':i',
  ],
  depends: hex_target,
)
