libkmsxxutil_sources = files([
    'src/colorbar.cpp',
    'src/color.cpp',
    'src/cpuframebuffer.cpp',
    'src/drawing.cpp',
    'src/extcpuframebuffer.cpp',
    'src/opts.cpp',
    'src/resourcemanager.cpp',
    'src/strhelpers.cpp',
    'src/testpat.cpp',
    'src/videodevice.cpp',
])

public_headers = [
    'inc/kms++util/color.h',
    'inc/kms++util/kms++util.h',
    'inc/kms++util/stopwatch.h',
    'inc/kms++util/cpuframebuffer.h',
    'inc/kms++util/strhelpers.h',
    'inc/kms++util/opts.h',
    'inc/kms++util/extcpuframebuffer.h',
    'inc/kms++util/resourcemanager.h',
    'inc/kms++util/videodevice.h',
]

private_includes = include_directories('src', 'inc')
public_includes = include_directories('inc')

compiler = meson.get_compiler('cpp')

# XXX: We use c99 array designator in one file. Disable the warning.
if compiler.has_argument('-Wno-c99-designator')
    libkmsxxutil_cpp_args = [ '-Wno-c99-designator' ]
else
    libkmsxxutil_cpp_args = [ ]
endif

libkmsxxutil_deps = [ libkmsxx_dep, libfmt_dep ]

libkmsxxutil = library('kms++util',
                       libkmsxxutil_sources,
                       cpp_args : libkmsxxutil_cpp_args,
                       install : true,
                       include_directories : private_includes,
                       dependencies : libkmsxxutil_deps)

libkmsxxutil_dep = declare_dependency(include_directories : public_includes,
                                      link_with : libkmsxxutil)

install_headers(public_headers, subdir : 'kms++util')

pkg = import('pkgconfig')
pkg.generate(libkmsxxutil)
