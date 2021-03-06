# (c) 2008-2011 by Anton Korenyushkin

vars = Variables()
vars.Add('mode', 'build mode (common, fast, debug, cov)', 'common')

COMMON_FLAGS = {
    'CXX': 'g++-4.3', # g++-4.4 fails to compile src/parser.cc
    'CCFLAGS': '-pedantic -Wall -Wextra -Werror'.split(),
    'CPPPATH': '.',
    'LINKCOM':
        ('$LINK -o $TARGET $LINKFLAGS $SOURCES $_LIBDIRFLAGS '
         '-Wl,-Bstatic $_LIBFLAGS -Wl,-Bdynamic -lpthread -lpq'),
    'LIBS': [
        'pqxx',
        'boost_program_options-mt',
        'http_parser',
        'git2',
        ],
    'variables': vars,
    }

MODE_FLAGS = {
    'common': {
        'LIBS': ['v8'],
        'LINKFLAGS': ['-rdynamic'],
        },
    'fast': {
        'LIBS': ['v8'],
        'CCFLAGS': ['-O3', '-DNDEBUG'],
        },
    'debug': {
        'LIBS': ['v8_g'],
        'CCFLAGS': ['-ggdb3'],
        'LINKFLAGS': ['-rdynamic'],
        },
    'cov': {
        'LIBS': ['v8'],
        'CCFLAGS': ['-fprofile-arcs', '-ftest-coverage'],
        'LINKFLAGS': ['-fprofile-arcs', '-ftest-coverage'],
        },
    }

env = Environment(**COMMON_FLAGS)
mode = env['mode']
try:
    env.Append(**MODE_FLAGS[mode])
except KeyError:
    print 'Valid modes are common, fast, debug, and cov.'
    Exit(1)

db_objects, js_objects = SConscript(
    'src/SConscript',
    build_dir='obj/' + mode,
    exports='env',
    duplicate=False)

test_objects = SConscript(
    'test/SConscript',
    build_dir='obj/' + mode + '/test',
    exports='env',
    duplicate=False)

patsak = env.Program('exe/' + mode + '/patsak', db_objects + js_objects)
env.Alias('patsak', patsak)

test_patsak = env.Program(
    'exe/' + mode + '/test-patsak',
    db_objects + test_objects,
    LIBS=env['LIBS'] + ['boost_unit_test_framework-mt'])
env.Alias('test-patsak', test_patsak)

all = env.Alias('all', [patsak, test_patsak])
env.Default(all)

env.AlwaysBuild(env.Alias('test', all, 'test/test.py ' + mode))

env.AlwaysBuild(env.Alias('clean', None, 'rm -rf obj exe cov'))

if mode == 'cov':
    cov_info = env.Command(
        'cov/cov.info',
        all,
        ('rm -rf cov obj/cov/*.gcda obj/cov/test/*.gcda;'
         'mkdir cov;'
         'test/test.py cov;'
         'lcov -d obj/cov/ -c -b . -o $TARGET'))
    env.AlwaysBuild(cov_info)
    env.Alias(
        'cov',
        env.Command('cov/index.html', cov_info, 'genhtml -o cov $SOURCE'))
