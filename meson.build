project('kms++', 'cpp',
    default_options : [
        'cpp_std=c++17',
    ],
)

cpp = meson.get_compiler('cpp')

if cpp.get_id() == 'gcc'
    if (host_machine.cpu_family() == 'arm' and
        cpp.version().version_compare('>=7.1') and
        cpp.version().version_compare('<9'))
        cpp_arguments += [
            '-Wno-psabi',
        ]
    endif
endif

if cpp.has_argument('-Wno-c99-designator')
    cpp_arguments += [
        '-Wno-c99-designator',
    ]
endif

libfmt_includes = include_directories('ext/fmt/include')
libfmt_dep = declare_dependency(include_directories : libfmt_includes,
                                compile_args : '-DFMT_HEADER_ONLY')

pybind11_includes = include_directories('ext/pybind11/include')
pybind11_dep = declare_dependency(include_directories : pybind11_includes)

libdrmomap_dep = dependency('libdrm_omap', required : get_option('omap'))

subdir('kms++')
subdir('kms++util')
subdir('utils')
subdir('py')

if get_option('kmscube')
    subdir('kmscube')
endif
