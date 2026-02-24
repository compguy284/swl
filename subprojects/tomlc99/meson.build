project('tomlc99', 'c',
  version: '1.0',
  default_options: ['c_std=c11'],
)

tomlc99_lib = static_library('tomlc99',
  'toml.c',
  include_directories: include_directories('.'),
)

tomlc99_dep = declare_dependency(
  link_with: tomlc99_lib,
  include_directories: include_directories('.'),
)
