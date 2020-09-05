project('kms++', 'c', 'cpp',
    default_options : [
        'cpp_std=c++17',
    ],
)

libfmt_includes = include_directories('ext/fmt/include')
libfmt_dep = declare_dependency(include_directories : libfmt_includes,
                                compile_args : '-DFMT_HEADER_ONLY')

libdrmomap_dep = dependency('libdrm_omap', required : get_option('omap'))

subdir('kms++')
subdir('kms++util')
subdir('utils')
subdir('py')

if get_option('kmscube')
    subdir('kmscube')
endif

