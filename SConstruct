
# (c) 2008 by Anton Korenyushkin

from os.path import join

vars = Variables()
vars.Add('mode', 'build mode (release, debug, cov)', 'release')

COMMON_FLAGS = {
    'CXX': 'g++-4.3', # g++-4.4 fails to compile src/parser.cc
    'CCFLAGS': '-pedantic -Wall -Werror -W -Wno-long-long'.split(),
    'LIBS': [
        'pthread',
        'pqxx',
        'boost_date_time-mt',
        'boost_program_options-mt',
        ],
    'variables': vars,
    }

MODE_FLAGS = {
    'release': {
        'LIBS': ['v8'],
        'CCFLAGS': ['-O0'],
        },
    'debug': {
        'LIBS': ['v8_g'],
        'CCFLAGS': ['-g', '-O0', '-DBACKTRACE', '-DTEST'],
        'LINKFLAGS': ['-rdynamic'],
        },
    'cov': {
        'LIBS': ['v8'],
        'CCFLAGS': ['-fprofile-arcs', '-ftest-coverage', '-DTEST'],
        'LINKFLAGS': ['-fprofile-arcs', '-ftest-coverage'],
        },
    }

################################################################################

env = Environment(**COMMON_FLAGS)
env.Append(**MODE_FLAGS[env['mode']])
env.Append(CPPPATH=['.'])

test_env = env.Clone()
test_env.Append(LIBS=['boost_unit_test_framework-mt'])

################################################################################

def invoke_script(dir, obj_dir, exports):
    return SConscript(join(dir, 'SConscript'),
                      build_dir=join('obj', env['mode'], obj_dir),
                      exports=exports,
                      duplicate=False)

objects = invoke_script('src', '', 'env')
test_objects = invoke_script('test', 'test', 'test_env')
sample_files = SConscript('sample/code/SConscript', duplicate=False)

main_obj = env.Object(target=join('obj', env['mode'], 'main.o'),
                      source='src/main.cc')

################################################################################

def program_path(name):
    return join('exe', env['mode'], name)

program = env.Program(program_path('patsak'), objects + [main_obj])
test_program = test_env.Program(program_path('test-patsak'),
                                objects + test_objects)

################################################################################

env.Alias('prog', program)
env.Alias('test-prog', test_program)
all = env.Alias('all', [program, test_program])
env.Default(all)

env.AlwaysBuild(env.Alias('test',
                          all,
                          'python test/test.py exe/' + env['mode']))

env.AlwaysBuild(env.Alias('doc', None, 'doxygen'))
env.AlwaysBuild(env.Alias('clean', None, 'rm -rf obj exe cov doc'))

################################################################################

if env['mode'] == 'cov':
    cov_info = env.Command('cov/cov.info',
                           [all, sample_files, 'test/test.py'],
                           'rm obj/cov/*.gcda obj/cov/test/*.gcda;'
                           'python test/test.py exe/cov;'
                           'lcov -d obj/cov/ -c -b . -o $TARGET')
    cov_html = env.Command('cov/index.html', cov_info,
                           'genhtml -o cov $SOURCE')
    env.Alias('cov', cov_html)
