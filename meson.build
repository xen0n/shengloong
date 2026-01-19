project(
  'shengloong',
  'c',
  version: '0.2.2',
  license: 'GPL-3.0-or-later',
  default_options: [
    'c_std=c99',
    'warning_level=3',
  ],
  meson_version: '>=0.61.0',
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

# i18n
subdir('po')

config_data = configuration_data()
config_data.set_quoted('CONFIG_LOCALEDIR', get_option('prefix') / get_option('localedir'))

if get_option('nls').require(
  dependency('intl').found(), error_message: 'NLS explicitly requested but no intl',
).allowed()
  # build options that need gettext
  config_data.set('ENABLE_NLS', 1)
  deps += [dependency('intl')]
else
  config_data.set('ENABLE_NLS', 0)
endif

config_h = configure_file(output: 'buildconfig.gen.h', configuration: config_data)

# Generate ISA extension classifier from loongarch-opcodes data
gen_isa_ext_classifier = custom_target(
  'gen-isa-ext-classifier',
  output: 'isa_ext_classifier.gen.h',
  input: [
    'tools/loongarch-opcodes/lsx.txt',
    'tools/loongarch-opcodes/lasx.txt',
    'tools/loongarch-opcodes/lbt.txt',
    'tools/loongarch-opcodes/lvz.txt',
  ],
  command: [
    find_program('python3'),
    meson.project_source_root() / 'tools/gen-isa-ext-classifier.py',
    meson.project_source_root() / 'tools/loongarch-opcodes',
    '@OUTPUT@',
  ],
)

# main executable
sl = executable(
  'shengloong',

  'src/cfg.c',
  'src/ctx.c',
  'src/main.c',
  'src/processing.c',
  'src/processing_abi_features.c',
  'src/processing_isa_ext.c',
  'src/processing_ldso.c',
  'src/processing_objabi.c',
  'src/processing_symbol_versions.c',
  'src/processing_syscall_abi.c',
  'src/utils.c',
  'src/walkdir.c',
  config_h,
  gen_isa_ext_classifier,

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
