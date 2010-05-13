#!/usr/bin/env/python

# (c) 2009-2010 by Anton Korenyushkin

''' Python test entry point '''

from __future__ import with_statement
import subprocess
import os.path
import unittest
import sys
import socket
import time
import os
import shutil
import psycopg2


TMP_DIR       = '/tmp/patsak'
SOCKET_DIR    = os.path.join(TMP_DIR, 'sockets')
LOG_FILE      = os.path.join(TMP_DIR, 'log')
GUARD_DIR     = os.path.join(TMP_DIR, 'guards')
MEDIA_DIR     = os.path.join(TMP_DIR, 'media')
CONFIG_FILE   = os.path.join(TMP_DIR, 'config')
APP_NAME      = 'test-app'
BAD_APP_NAME  = 'bad-app'
WAIT_APP_NAME = 'lib'
USER_NAME     = 'test-user'
SPOT_NAME     = 'test-spot'
DB_NAME       = 'test_patsak'
DB_USER       = 'test'
DB_PASSWORD   = 'test'
DB_PARAMS     = 'user=%s password=%s dbname=%%s' % (DB_USER, DB_PASSWORD)
TEST_DIR      = os.path.dirname(__file__)
RELEASE_DIR   = os.path.join(TEST_DIR, '../sample/release')
INIT_DB_FILE  = os.path.join(TEST_DIR, 'init-db.sql')
FUNCS_FILE    = os.path.join(TEST_DIR, '../src/funcs.sql')


class _Response:
    def __init__(self, status, data):
        self.status = status
        self.data = data


def _popen(*args, **kwds):
    kwds.update({'stdin': subprocess.PIPE,
                 'stdout': subprocess.PIPE,
                 'stderr': subprocess.PIPE,
                 })
    return subprocess.Popen(*args, **kwds)
                 

class Test(unittest.TestCase):
    def _check_launch(self, args, code=0, program=None):
        process = _popen([program or PATSAK_FILE,
                          '--config-file', CONFIG_FILE] + args)
        self.assertEqual(process.wait(), code)

    def testTestPatsak(self):
        self._check_launch([], 0, TEST_PATSAK_FILE)

    def testMain(self):
        self._check_launch([], 1)
        self._check_launch(['--unknown-option', APP_NAME], 1)
        self._check_launch(['--help'])
        self._check_launch(['--rev'])
        self._check_launch(['--code-dir', '', APP_NAME], 1)
        self._check_launch(['--expr', '2+2', APP_NAME], 1)
        self._check_launch(['--test'], 1)
        self._check_launch(['--test', APP_NAME, USER_NAME], 1)

    def _eval(self, expr):
        process = _popen([PATSAK_FILE,
                          '--config-file', CONFIG_FILE,
                          '--test',
                          '--expr', expr,
                          APP_NAME])
        status = process.stdout.readline()[:-1]
        return _Response(status, process.stdout.read()[:-1])

    def testJS(self):
        self.assertEqual(self._eval('argh!!!!').status, 'ERROR')
        pish_result = self._eval('pish')
        self.assertEqual(pish_result.status, 'ERROR')
        self.assert_(pish_result.data.startswith(
                'Line 1, column 0\nReferenceError: pish is not defined\n'))
        self.assertEqual(self._eval('2+2').data, '4')
        self.assertEqual(self._eval('s="x"; while(1) s+=s').data,
                         '<Out of memory>')
        self._check_launch(['--test', '--expr', '2+2', 'no-such-app'], 1)
        self._check_launch(['--test', '--expr', '2+2\n3+3', APP_NAME])
        self.assertEqual(self._eval('bug()').status, 'ERROR')

        process = _popen([PATSAK_FILE,
                          '--config-file', CONFIG_FILE,
                          '--test',
                          APP_NAME])
        process.stdin.write('2+2\n')
        process.stdin.close()
        self.assertEqual(process.stdout.read(), 'OK\n4\n')
        self.assertEqual(process.wait(), 0)

        process = _popen([PATSAK_FILE,
                          '--config-file', CONFIG_FILE,
                          '--test',
                          '--expr', '2+2',
                          BAD_APP_NAME])
        self.assertEqual(process.stdout.read().split('\n')[0], 'ERROR')
        self.assertEqual(process.wait(), 0)
        process = _popen([PATSAK_FILE,
                          '--config-file', CONFIG_FILE,
                          '--patsak-pattern', PATSAK_FILE,
                          '--test',
                          '--expr',
                          '_core.requestApp("another-app", "2+2", [], "")',
                          APP_NAME])
        self.assertEqual(process.stdout.read(), 'OK\n4\n')
        
    def _connect(self, path):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(path)
        return sock

    def _talk_through_socket(self, path, message):
        sock = self._connect(path)
        sock.send(message + '\n')
        result = sock.recv(4096)
        sock.close()
        self.assert_(len(result) < 4096)
        return result

    def testReleaseServer(self):
        # populating media dir with initial data to test FSBg initialization
        os.mkdir(MEDIA_DIR + '/release/test-app/dir')
        open(MEDIA_DIR + '/release/test-app/dir/file', 'w').write('hello')
        
        _popen([PATSAK_FILE,
                '--config-file', CONFIG_FILE,
                '--wait', '1',
                WAIT_APP_NAME])
        process = _popen([PATSAK_FILE,
                          '--config-file', CONFIG_FILE,
                          APP_NAME])
        self.assertEqual(process.stdout.readline(), 'READY\n')
        self.assertEqual(process.wait(), 0)

        socket_path = os.path.join(SOCKET_DIR, 'release', APP_NAME)
        sock = self._connect(socket_path)
        sock.close()
        sock = self._connect(socket_path)
        sock.send('STATUS\n')
        sock.close()

        def talk(message):
            return self._talk_through_socket(socket_path, message)
        
        self.assertEqual(talk('STATUS'), 'OK\n')
        
        self.assertEqual(talk('PROCESS 2+2'), 'OK\n4')
        self.assertEqual(talk('PROCESS\nDATA 0\n\nREQUEST 3\n2+2'), 'OK\n4')
        self.assertEqual(talk('PROCESS\nUSER anton\nEXPR 6\nmain()'), 'OK\n0')
        self.assertEqual(talk('PROCESS\nREQUEST 3\n2+2'), 'OK\n4')
        self.assertEqual(talk('PROCESS _core.data.length'), 'OK\n0')
        self.assertEqual(talk('PROCESS\nREQUEST 10\n_core.data'), 'OK\n')
        self.assertEqual(talk('PROCESS\nDATA 5\nhello\n'
                              'REQUEST 10\n_core.data'),
                         'OK\nhello')
        self.assertEqual(talk('PROCESS new _core.Binary(3)'), 'OK\n\0\0\0')

        self.assert_(
            'File is read only' in
            talk('PROCESS\nFILE %s\nREQUEST 25\n_core.files[0]._write("")'
                 % CONFIG_FILE))
        request = '''
_core.requestApp(
  'another-app', '_core.files[0]._read()._toString()', _core.files, '')
'''
        self.assertEqual(
            talk('PROCESS\nFILE %s\nREQUEST %d\n%s'
                 % (CONFIG_FILE, len(request), request)),
            'OK\n' + open(CONFIG_FILE).read())

        self.assertEqual(
            talk('PROCESS _core.db.create("xxx", {}, [], [], []); throw 1'),
            'ERROR\nLine 1, column 41\nUncaught 1')
        self.assertEqual(talk('PROCESS "xxx" in _core.db.list()'), 'OK\nfalse')
        self.assertEqual(
            talk('PROCESS '
                 '_core.db.create("xxx", {}, [], [], []);'
                 '_core.db.rollback()'),
            'OK\nundefined')
        self.assertEqual(talk('PROCESS "xxx" in _core.db.list()'), 'OK\nfalse')

        # Timed out test. Long to run.
#         self.assertEqual(talk('PROCESS for (;;) main();'),
#                          'ERROR\n<Timed out>')
        
        sock = self._connect(socket_path)
        sock.send('PROCESS\nREQUEST 3\n')
        time.sleep(0.1)
        sock.send('2+2')
        self.assertEqual(sock.recv(4096), 'OK\n4')
        sock.close()
        
        self.assertEqual(talk('UNKNOWN'),
                         'FAIL\nUnknown request: UNKNOWN')
        self.assertEqual(talk('PROCESS\nREQUEST 3 hi!\n2+2'),
                         'FAIL\nIll formed request')
        self.assertEqual(talk('PROCESS\nSTRANGE_CMD\n'),
                         'FAIL\nUnexpected command: STRANGE_CMD')
        self.assertEqual(talk('PROCESS\nFILE\tpish\nREQUEST 2+2'),
                         'FAIL\nBad command tail')
        self.assertEqual(talk('PROCESS\nDATA 1\n1\nEXPR 3\n2+2'),
                         'FAIL\nDATA is not supported by EXPR')
        self.assertEqual(talk('PROCESS\nFILE file\nEXPR 3\n2+2'),
                         'FAIL\nFILE is not supported by EXPR')
        self.assertEqual(talk('PROCESS\nISSUER test-app\nEXPR 3\n2+2'),
                         'FAIL\nISSUER is not supported by EXPR')
        
        self.assertEqual(talk('PROCESS _core.app.spot'),
                         'OK\nundefined')
        self.assertEqual(talk('STOP'), 'OK\n')
        self.assertRaises(socket.error, self._connect, socket_path)
        # 1 second should elapsed
        self.assertRaises(socket.error, self._connect,
                          os.path.join(SOCKET_DIR, 'release', WAIT_APP_NAME))

    def testSpotServer(self):
        process = _popen([PATSAK_FILE,
                          '--config-file', CONFIG_FILE,
                          '--pass-opts',
                          APP_NAME, USER_NAME, SPOT_NAME])
        self.assertEqual(process.stdout.readline(), 'READY\n')
        socket_path = os.path.join(SOCKET_DIR, 'spots',
                                   APP_NAME, USER_NAME, SPOT_NAME)
        def talk(message):
            return self._talk_through_socket(socket_path, message)
        
        self.assertEqual(talk('PROCESS\nREQUEST 1\n1'),
                         'ERROR\nmain is not a function')
        self.assertEqual(talk('PROCESS pass'), 'OK\ntrue')
        self.assertEqual(
            talk('PROCESS _core.owner + " " + _core.spot'),
            'OK\ntest user test-spot')
        self.assertEqual(
            talk('PROCESS '
                 '_core.requestApp("another-app", "_core.issuer", [], "")'),
            'OK\n' + APP_NAME)
        self.assertEqual(
            talk('PROCESS function f() { f(); } f()'),
            'ERROR\nRangeError: Maximum call stack size exceeded')
        self.assertEqual(talk('PROCESS s="x"; while(1) s+=s'),
                         'ERROR\n<Out of memory>')
        self.assertRaises(socket.error, self._connect, socket_path)
        
    def testVersion01(self):
        process = _popen([PATSAK_FILE,
                          '--config-file', CONFIG_FILE,
                          '--patsak-pattern', '../patsak%s/exe/release/patsak',
                          '--test',
                          '--expr', '_core.requestApp("app01", "", [], "")',
                          APP_NAME])
        self.assertEqual(process.stdout.read(), 'OK\nHello from 0.1!\n')

        
def _create_schema(cursor, schema_name):
    cursor.execute('DROP SCHEMA IF EXISTS "%s" CASCADE' % schema_name)
    cursor.execute('SELECT ku.create_schema(%s)', (schema_name,))
    
        
def _create_db():
    conn = psycopg2.connect(DB_PARAMS % 'template1')
    conn.set_isolation_level(0)
    cursor = conn.cursor()
    try:
        cursor.execute('CREATE DATABASE "%s"' % DB_NAME)
    except psycopg2.Error, error:
        if error.pgcode != '42P04':
            raise
    conn.close()
    conn = psycopg2.connect(DB_PARAMS % DB_NAME)
    cursor = conn.cursor()
    cursor.execute(open(INIT_DB_FILE).read())
    cursor.execute(open(FUNCS_FILE).read())
    for app_name in os.listdir(RELEASE_DIR):
        if (app_name[0] == '.' or
            not os.path.isdir(os.path.join(RELEASE_DIR, app_name))):
            continue
        _create_schema(cursor, ':' + app_name)
    _create_schema(cursor, ':%s:%s:%s' % (APP_NAME, USER_NAME, SPOT_NAME))
    conn.commit()
    conn.close()


def _drop_db():
    conn = psycopg2.connect(DB_PARAMS % 'template1')
    conn.set_isolation_level(0)
    cursor = conn.cursor()
    try:
        cursor.execute('DROP DATABASE "%s"' % DB_NAME)
    except psycopg2.ProgrammingError, error:
        if error.pgcode != '3D000':
            raise
    conn.close()

    
def _make_dir_tree(base_dir):
    os.makedirs(os.path.join(base_dir, 'release'));
    os.makedirs(os.path.join(base_dir, 'spots', APP_NAME, USER_NAME))

    
def _make_dirs():
    if os.path.exists(TMP_DIR):
        shutil.rmtree(TMP_DIR)
    _make_dir_tree(SOCKET_DIR)
    _make_dir_tree(GUARD_DIR)
    _make_dir_tree(MEDIA_DIR)
    os.mkdir(os.path.join(MEDIA_DIR, 'release', APP_NAME))

    
def _write_config():
    with open(CONFIG_FILE, 'w') as f:
        f.write('''
db-name=%s
db-user=%s
db-password=%s
code-dir=%s/../sample
socket-dir=%s
guard-dir=%s
media-dir=%s
log-file=%s
''' % (DB_NAME, DB_USER, DB_PASSWORD,
       os.path.abspath(TEST_DIR),
       SOCKET_DIR, GUARD_DIR, MEDIA_DIR,
       LOG_FILE))

    
def main():
    if len(sys.argv) != 2:
        print 'Usage: ', sys.argv[0], ' dir'
        sys.exit(1)
    global PATSAK_FILE, TEST_PATSAK_FILE
    PATSAK_FILE = os.path.join(sys.argv[1], 'patsak')
    TEST_PATSAK_FILE = os.path.join(sys.argv[1], 'test-patsak')
    suite = unittest.TestLoader().loadTestsFromTestCase(Test)
    _drop_db()
    _create_db()
    _make_dirs()
    _write_config()
    
    unittest.TextTestRunner(verbosity=2).run(suite)
    
    _popen(['killall', '-w', 'patsak'])
    shutil.rmtree(TMP_DIR)
    
        
if __name__ == '__main__':
    main()
