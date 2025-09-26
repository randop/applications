project('arduino-blink', ['c', 'cpp'],  # Languages as a list for mixed C/C++
  version: '0.1',
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
  meson_version: '>=0.54.0',
)

# Assert cross-build (Arduino requires it)
assert(meson.is_cross_build(), 'Arduino projects require cross-compilation.')

# Core sources (minimal for Blink: main() + wiring + digital I/O)
core_sources = [
  'ArduinoCore-avr/cores/arduino/main.cpp',
  'ArduinoCore-avr/cores/arduino/hooks.c',
  'ArduinoCore-avr/cores/arduino/wiring.c',
  'ArduinoCore-avr/cores/arduino/wiring_digital.c',
]

# source sketch
sketch_sources = ['Blink.cpp']

arduino_includes = include_directories('ArduinoCore-avr/cores/arduino') 
variant_standard_includes = include_directories('ArduinoCore-avr/variants/standard')

# Executable (ELF)
exe = executable('blink',
  sources: core_sources + sketch_sources,
  name_suffix: 'elf',
  include_directories : [arduino_includes, variant_standard_includes]
)

# Generate HEX from ELF
objcopy = find_program('avr-objcopy')
hex_target = custom_target('blink.hex',
  input: exe,
  output: 'blink.hex',
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
