project('quic-rproxy', 'c',
  version: '0.1.0',
  default_options: ['c_std=c11'],
  meson_version: '>=1.0.0')

# Get the C compiler
cc = meson.get_compiler('c')

cmake = import('cmake')
fs = import('fs')

# Pre-built BoringSSL paths (adjust if installed elsewhere)
boringssl_root = '/opt/boringssl/current'  # Source root (with include/)
boringssl_build = '/opt/boringssl/9fc1c33e9c21439ce5f87855a6591a9324e569fd/build'  # Build dir (with libs)
libssl_path = boringssl_build / 'ssl' / 'libssl.a'
libcrypto_path = boringssl_build / 'crypto' / 'libcrypto.a'

# Validate libs exist (fail config if missing)
if not fs.exists(libssl_path) or not fs.exists(libcrypto_path)
  error('BoringSSL libs not found at ' + libssl_path + '. Build manually first.')
endif

# Manually declare BoringSSL dependencies
boringssl_inc = include_directories(boringssl_root / 'include')  # Generated includes
ssl_link_args = [libssl_path, libcrypto_path]
ssl_dep = declare_dependency(include_directories: boringssl_inc, link_args: ssl_link_args)
crypto_dep = declare_dependency(include_directories: boringssl_inc, link_args: [libcrypto_path])

# LSQUIC subproject (uses pre-built BoringSSL; expects libs in subdirs)
lsquic_opts = cmake.subproject_options()
lsquic_opts.add_cmake_defines({
  'LIBSSL_DIR': boringssl_root,  # LSQUIC searches here for include/ + ssl/crypto/*.a
  'CMAKE_BUILD_TYPE': 'Release'
})
lsquic_subproj = cmake.subproject('lsquic', options: lsquic_opts)
lsquic_dep = lsquic_subproj.dependency('lsquic')

# Zlib dependency (system-installed)
zlib_dep = dependency('zlib')

event_dep = dependency('libevent', required: true)

m_dep = cc.find_library('m', required : false)

# Common dependencies
deps = [lsquic_dep, ssl_dep, crypto_dep, zlib_dep, event_dep, m_dep]

subdir('server')
subdir('client')
