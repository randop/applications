fs = import('fs')

project(
    'spqk',
    'cpp',
    version: fs.read('VERSION').strip(),
    default_options: ['cpp_std=c++23'],
)

espeak = dependency('espeak-ng')
boost_program_options = dependency('boost', modules: ['program_options'])
boost_json = dependency('boost', modules: ['json'])
spdlog_project = subproject(
    'spdlog',
    default_options: ['std_format=enabled', 'tests=disabled'],
)
spdlog = spdlog_project.get_variable('spdlog_dep')

config_data = configuration_data()
config_data.set('SPQK_VERSION', '"' + meson.project_version() + '"')
configure_file(output: 'config.h', configuration: config_data)

executable(
    'spqk',
    'main.cpp',
    dependencies: [espeak, boost_program_options, boost_json, spdlog],
    install: false,
)
