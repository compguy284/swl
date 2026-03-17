project(
  'swl',
  'c',
  version: '0.8-dev',
  default_options: [
    'c_std=c23',
    'warning_level=3',
  ],
  meson_version: '>=1.1.0',
)

cc = meson.get_compiler('c')

add_project_arguments(
  cc.get_supported_arguments(
    '-Wpedantic',
    '-Wshadow',
    '-Wunused-macros',
    '-Wfloat-conversion',
    '-Werror=strict-prototypes',
    '-Werror=implicit',
    '-Werror=return-type',
    '-Werror=incompatible-pointer-types',
    '-Wno-unused-parameter',
  ),
  language: 'c',
)

add_project_arguments(
  '-DWLR_USE_UNSTABLE',
  '-D_POSIX_C_SOURCE=200809L',
  language: 'c',
)

# Git version detection
git = find_program('git', required: false)
if git.found()
  git_describe = run_command(git, 'describe', '--tags', '--dirty', check: false)
  if git_describe.returncode() == 0
    version = git_describe.stdout().strip()
  else
    version = meson.project_version()
  endif
else
  version = meson.project_version()
endif
git_hash = get_option('git_hash')
if git_hash != ''
  version = version + '-' + git_hash
endif
add_project_arguments('-DVERSION="@0@"'.format(version), language: 'c')

# Dependencies
wayland_server = dependency('wayland-server')
wayland_protos = dependency('wayland-protocols')
scenefx = dependency('scenefx-0.4', include_type: 'system')
wlroots = dependency('wlroots-0.19')
xkbcommon = dependency('xkbcommon')
libinput = dependency('libinput')
math = cc.find_library('m')

deps = [wayland_server, scenefx, wlroots, xkbcommon, libinput, math]

# XWayland support
xwayland = get_option('xwayland')
if xwayland.enabled()
  xcb = dependency('xcb')
  xcb_icccm = dependency('xcb-icccm')
  deps += [xcb, xcb_icccm]
  add_project_arguments('-DXWAYLAND', language: 'c')
endif

# Protocol header generation
wayland_scanner = find_program('wayland-scanner')
wayland_protos_dir = wayland_protos.get_variable('pkgdatadir')

protocols = {
  'xdg-shell-protocol': {
    'xml': wayland_protos_dir / 'stable/xdg-shell/xdg-shell.xml',
    'type': 'server-header',
  },
  'cursor-shape-v1-protocol': {
    'xml': wayland_protos_dir / 'staging/cursor-shape/cursor-shape-v1.xml',
    'type': 'enum-header',
  },
  'pointer-constraints-unstable-v1-protocol': {
    'xml': wayland_protos_dir / 'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml',
    'type': 'enum-header',
  },
  'wlr-layer-shell-unstable-v1-protocol': {
    'xml': 'protocols/wlr-layer-shell-unstable-v1.xml',
    'type': 'enum-header',
  },
  'wlr-output-power-management-unstable-v1-protocol': {
    'xml': 'protocols/wlr-output-power-management-unstable-v1.xml',
    'type': 'server-header',
  },
}

protocol_headers = []
foreach name, proto : protocols
  protocol_headers += custom_target(
    name,
    input: proto['xml'],
    output: name + '.h',
    command: [wayland_scanner, proto['type'], '@INPUT@', '@OUTPUT@'],
  )
endforeach

# Sources
src = files(
  'src/main.c',
  'src/server.c',
  'src/config.c',
  'src/client.c',
  'src/commands.c',
  'src/cursor.c',
  'src/clipboard.c',
  'src/idle.c',
  'src/input.c',
  'src/layer.c',
  'src/layout.c',
  'src/scroller.c',
  'src/ipc.c',
  'src/output.c',
  'src/session.c',
  'src/util.c',
)

toml_lib = static_library(
  'toml',
  'src/toml.c',
  c_args: ['-Wno-unused-macros'],
)

if xwayland.enabled()
  src += files('src/xwayland.c')
endif

swl = executable(
  'swl',
  src,
  protocol_headers,
  include_directories: include_directories('src'),
  dependencies: deps,
  link_with: toml_lib,
  install: true,
)

swlmsg = executable(
  'swlmsg',
  'tools/swlmsg.c',
  install: true,
)

# Install man page
install_man('swl.1')

# Install desktop file
install_data(
  'swl.desktop',
  install_dir: get_option('datadir') / 'wayland-sessions',
)

# Install default config
install_data(
  'config/default.toml',
  install_dir: get_option('datadir') / 'swl',
)
