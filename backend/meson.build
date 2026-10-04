project('omarchy-xrdp-backend', 'c',
  version: '0.2.0',
  license: 'MIT',
  default_options: ['c_std=c11', 'warning_level=2', 'buildtype=release'])

add_project_arguments('-D_GNU_SOURCE', '-Wno-unused-parameter', language: 'c')

wayland_client = dependency('wayland-client')
xkbcommon = dependency('xkbcommon')
wayland_protocols = dependency('wayland-protocols')
wayland_scanner = find_program('wayland-scanner')

wp_dir = wayland_protocols.get_variable('pkgdatadir')
protocols = [
  wp_dir / 'staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml',
  wp_dir / 'staging/ext-image-capture-source/ext-image-capture-source-v1.xml',
  wp_dir / 'staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml',
  'protocols/virtual-keyboard-unstable-v1.xml',
  'protocols/wlr-virtual-pointer-unstable-v1.xml',
]

protocol_sources = []
foreach xml : protocols
  name = xml.split('/')[-1].replace('.xml', '')
  protocol_sources += custom_target(name + '-client-header',
    input: xml, output: name + '-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
  protocol_sources += custom_target(name + '-code',
    input: xml, output: name + '-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
endforeach

executable('omarchy-xrdp-backend',
  'src/main.c', 'src/wayland.c', 'src/cursor.c', 'src/input.c', 'src/xup.c', 'src/keymap.c',
  'src/color.c',
  protocol_sources,
  dependencies: [wayland_client, xkbcommon])
