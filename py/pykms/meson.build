pymod = import('python')
py3_inst = pymod.find_installation('python3', required : get_option('pykms'))

if py3_inst.found() == false
    subdir_done()
endif

py3_dep = py3_inst.dependency()

if py3_dep.found() == false
    subdir_done()
endif

pykms_sources = files([
    'pykmsbase.cpp',
    'pykms.cpp',
    'pykmsutil.cpp',
    'pyvid.cpp',
])

if libdrmomap_dep.found()
    pykms_sources += files([
        'pykmsomap.cpp',
    ])
endif

pykms_deps = [
    libkmsxx_dep,
    libkmsxxutil_dep,
    py3_dep,
]

includes = [
    '../../ext/pybind11/include',
]

pykms = py3_inst.extension_module('pykms',
                                  pykms_sources,
                                  install : true,
                                  subdir : 'pykms',
                                  include_directories : includes,
                                  dependencies : pykms_deps)

# Copy __init__.py to build dir so that we can run without installing
configure_file(input: '__init__.py', output: '__init__.py', copy: true)

py3_inst.install_sources('__init__.py', subdir : 'pykms', pure : false)
