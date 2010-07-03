
# (c) 2008-2010 by Anton Korenyushkin

from os.path import join
from subprocess import Popen, PIPE

vars = Variables()
vars.Add('mode', 'build mode (release, debug, cov)', 'release')

COMMON_FLAGS = {
    'CXX': 'g++-4.3', # g++-4.4 fails to compile src/parser.cc
    'CCFLAGS': '-pedantic -Wall -Werror -W -Wno-long-long'.split(),
    'CPPPATH': '.',
    'LINKCOM': ('$LINK -o $TARGET $LINKFLAGS $SOURCES $_LIBDIRFLAGS '
                '-Wl,-Bstatic $_LIBFLAGS -Wl,-Bdynamic -lpthread -lpq'),
    'LIBS': [
        'pqxx',
        'boost_date_time-mt',
        'boost_program_options-mt',
        'boost_thread-mt',
        'http_parser',
        'git2',
        ],
    'variables': vars,
    }

MODE_FLAGS = {
    'release': {
        'LIBS': ['v8'],
        'CCFLAGS': ['-O0', '-DBACKTRACE'],
        'LINKFLAGS': ['-rdynamic'],
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

test_env = env.Clone()
test_env.Append(LIBS=['boost_unit_test_framework-mt'])

################################################################################

def invoke_script(dir, obj_dir, exports):
    return SConscript(join(dir, 'SConscript'),
                      build_dir=join('obj', env['mode'], obj_dir),
                      exports=exports,
                      duplicate=False)

common_objects, js_objects = invoke_script('src', '', 'env')
test_objects = invoke_script('test', 'test', 'test_env')

revision = Popen(['git', 'rev-parse', '--short', 'HEAD'],
                 stdout=PIPE).stdout.read()[:-1]
main_env = env.Clone()
main_env.Append(CCFLAGS=['-DREVISION=\\"%s\\"' % revision])
main_obj = main_env.Object(target=join('obj', env['mode'], 'main.o'),
                           source='src/main.cc')

################################################################################

def program_path(name):
    return join('exe', env['mode'], name)

program = env.Program(program_path('patsak'),
                      common_objects + js_objects + [main_obj])

test_program = test_env.Program(program_path('test-patsak'),
                                common_objects + test_objects)

################################################################################

env.Alias('prog', program)
env.Alias('test-prog', test_program)
all = env.Alias('all', [program, test_program])
env.Default(all)

env.AlwaysBuild(env.Alias('test',
                          all,
                          'python test/test.py exe/' + env['mode']))

env.AlwaysBuild(env.Alias('clean', None, 'rm -rf obj exe cov'))

################################################################################

if env['mode'] == 'cov':
    cov_info = env.Command('cov/cov.info',
                           all,
                           'rm obj/cov/*.gcda obj/cov/test/*.gcda;'
                           'python test/test.py exe/cov;'
                           'lcov -d obj/cov/ -c -b . -o $TARGET')
    env.AlwaysBuild(cov_info)
    cov_html = env.Command('cov/index.html', cov_info,
                           'genhtml -o cov $SOURCE')
    env.Alias('cov', cov_html)
