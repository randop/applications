project('turbotui', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++20'])

# Import CMake module
cmake = import('cmake')

# Configure tvision subproject
tvision_opts = cmake.subproject_options()
tvision_opts.add_cmake_defines({'CMAKE_CXX_STANDARD': '20', 'CMAKE_BUILD_TYPE': 'Release'})
tvision_proj = cmake.subproject('tvision', options: tvision_opts)

# Get the dependency
dep = tvision_proj.dependency('tvision')

executable('turbotui',
           'src/main.cpp',
           dependencies : dep,
           install : true)