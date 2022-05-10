project('shengloong', 'c')

deps = [
  dependency('libelf'),
  dependency('popt'),
]

executable(
  'shengloong',

  'cfg.c',
  'ctx.c',
  'main.c',
  'utils.c',

  dependencies: deps,
  install: true,
)
