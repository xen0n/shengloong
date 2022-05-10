project(
  'shengloong',
  'c',
  version: '0.1.1',
  license: 'GPL-3.0-or-later',
  default_options: [
    'c_std=c99',
    'warning_level=3',
  ],
)

deps = [
  dependency('libelf'),
  dependency('popt'),
]

cflags = [
  '-D_GNU_SOURCE',
  '-D_XOPEN_SOURCE=500',
]
add_project_arguments(cflags, language: 'c')

executable(
  'shengloong',

  'src/cfg.c',
  'src/ctx.c',
  'src/main.c',
  'src/processing.c',
  'src/processing_ldso.c',
  'src/utils.c',
  'src/walkdir.c',

  dependencies: deps,
  install: true,
)
