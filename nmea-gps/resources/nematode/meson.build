project('NemaTode', 'cpp',
  version : '1.0.0',  # Approximate; no releases in repo
  default_options : ['cpp_std=c++11', 'warning_level=0'])

# Sources (based on repo structure: src/ for impl, include/ for headers)
# Adjust if repo changes—common files from inspection
nematode_sources = [
  'src/NMEAParser.cpp',
  'src/GPSService.cpp',
  'src/GPSFix.cpp',
  'src/NMEACommand.cpp',
  'src/NumberConversion.cpp'
]

nematode_headers = [
  'include/nmeaparse/NMEAParser.h',
  'include/nmeaparse/GPSService.h',
  'include/nmeaparse/Event.h',
  'include/nmeaparse/GPSFix.h',
  'include/nmeaparse/NMEACommand.h',
  'include/nmeaparse/nmea.h',
  'include/nmeaparse/NumberConversion.h',
]

# Public headers (in include/nmeaparse/)
nematode_inc = include_directories('include')

# Build as static library
nematode_lib = static_library('NemaTode',
  nematode_sources,
  include_directories : nematode_inc,
  install : true,
  gnu_symbol_visibility : 'hidden'  # Optional: for better encapsulation
)

# Export as dependency (matches [provide] in .wrap)
nematode_dep = declare_dependency(
  link_with : nematode_lib,
  include_directories : nematode_inc,
  sources : nematode_headers  # Ensures headers are available
)
