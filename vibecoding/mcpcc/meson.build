project('McpServer', 'cpp',
  version : '0.1.0',
  default_options : ['cpp_std=c++20'])

libuv_dep = dependency('libuv', version : '>=1.0.0')
nlohmann_json_dep = dependency('nlohmann_json', version : '>=3.0.0')

executable('mcpserver',
  'src/main.cpp',
  dependencies : [libuv_dep, nlohmann_json_dep],
  install : true)