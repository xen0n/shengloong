project(
  'shengloong',
  'c',
  version: '0.2.0',
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

sl = executable(
  'shengloong',

  'src/cfg.c',
  'src/ctx.c',
  'src/main.c',
  'src/processing.c',
  'src/processing_ldso.c',
  'src/processing_syscall_abi.c',
  'src/utils.c',
  'src/walkdir.c',

  dependencies: deps,
  install: true,
)

util_bfdhash = executable(
  'bfdhash',

  'src/utils.c',

  c_args: ['-DUTIL_BFDHASH'],
  build_by_default: false,
  install: false,
)

# Tests
test(
  'e2e-cli',
  find_program('./tests/e2e-cli.sh'),
  args: [sl],
  depends: [sl],
  suite: 'e2e',
)
test(
  'e2e-ignores',
  find_program('./tests/e2e-ignores/runtest.sh'),
  args: [sl],
  depends: [sl],
  suite: 'e2e',
)
test(
  'e2e-smoke',
  find_program('./tests/e2e-smoke/runtest.sh'),
  args: [sl],
  depends: [sl],
  suite: 'e2e',
)
test(
  'e2e-walk',
  find_program('./tests/e2e-walk/runtest.sh'),
  args: [sl],
  depends: [sl],
  suite: 'e2e',
)
