sigfm_tests = executable('sigfm-tests',
    ['tests.c', 'sigfm.c'],
    dependencies: mathlib_dep,
    c_args: '-Wno-unused-result')
test('sigfm', sigfm_tests, timeout: 180)
