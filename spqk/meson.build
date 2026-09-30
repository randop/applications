project(
    'spqk',
    'cpp',
    version: run_command('cat', 'VERSION', check: true).stdout().strip(),
    default_options: ['cpp_std=c++23'],
)

espeak = dependency('espeak-ng')
boost = dependency(
    'boost',
    modules: ['program_options', 'json', 'container'],
    version: '>=1.90.0',
)
sqlite = dependency('sqlite3')
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
    ['main.cpp', 'playback.cpp'],
    dependencies: [
        espeak,
        boost,
        sqlite,
        spdlog,
    ],
    install: false,
)
