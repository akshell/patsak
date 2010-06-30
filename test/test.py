#!/usr/bin/env/python

# (c) 2009-2010 by Anton Korenyushkin

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
import errno


TMP_PATH     = '/tmp/patsak'
LOG_PATH     = os.path.join(TMP_PATH, 'log')
SOCKET_PATH  = os.path.join(TMP_PATH, 'socket')
MEDIA_PATH   = os.path.join(TMP_PATH, 'media')
CONFIG_PATH  = os.path.join(TMP_PATH, 'config')
APP_NAME     = 'test-app'
USER_NAME    = 'test-user'
SPOT_NAME    = 'test-spot'
DB_NAME      = 'test'
DB_USER      = 'test'
DB_PASSWORD  = 'test'
DB_PARAMS    = 'user=%s password=%s dbname=%%s' % (DB_USER, DB_PASSWORD)
TEST_PATH    = os.path.dirname(__file__)
RELEASE_PATH = os.path.join(TEST_PATH, '../sample/release')
INIT_DB_PATH = os.path.join(TEST_PATH, 'init-db.sql')
FUNCS_PATH   = os.path.join(TEST_PATH, '../src/funcs.sql')


def _popen(*args, **kwds):
    kwds.update({'stdin': subprocess.PIPE,
                 'stdout': subprocess.PIPE,
                 'stderr': subprocess.PIPE,
                 })
    return subprocess.Popen(*args, **kwds)


class Test(unittest.TestCase):
    def _check_launch(self, args, code=0, program=None):
        process = _popen(
            [program or PATSAK_PATH, '--config-file', CONFIG_PATH] + args)
        self.assertEqual(process.wait(), code)

    def testTestPatsak(self):
        self._check_launch([], 0, TEST_PATSAK_PATH)

    def testMain(self):
        self._check_launch([], 1)
        self._check_launch(['--unknown-option'], 1)
        self._check_launch(['--help'])
        self._check_launch(['--rev'])
        self._check_launch(['serve'], 1)
        self._check_launch(['serve', 'socket'], 1)
        self._check_launch(['serve', 'socket', 'app', 'owner'], 1)
        self._check_launch(['--code-dir', '', 'serve', 'socket', 'app'], 1)
        process = _popen([PATSAK_PATH,
                          '--config-file', CONFIG_PATH,
                          'serve', 'no/such/path',
                          APP_NAME])
        self.assertEqual(process.stdout.read(), '')
        self.assertEqual(process.wait(), 0)

    def _eval(self, expr):
        process = _popen(
            [PATSAK_PATH, '--config-file', CONFIG_PATH, 'eval', expr, APP_NAME])
        return process.stdout.read()[:-1]

    def testJS(self):
        self.assert_('SyntaxError' in self._eval('argh!!!!'))
        self.assert_('ReferenceError' in self._eval('pish'))
        self.assertEqual(self._eval('2+2'), 'OK\n4')
        self.assertEqual(self._eval('2+2\n3+3'), 'OK\n6')
        self.assertEqual(self._eval('s="x"; while(1) s+=s'),
                         'ERROR\n<Out of memory>')
        self._check_launch(['eval', '2+2', 'no-such-app'], 1)
        self._check_launch(['eval', '2+2\n3+3', APP_NAME])

        process = _popen(
            [PATSAK_PATH, '--config-file', CONFIG_PATH, 'eval', '1', 'bad-app'])
        self.assert_('SyntaxError' in process.stdout.read())
        self.assertEqual(process.wait(), 0)

    def _connect(self):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(SOCKET_PATH)
        return sock

    def _talk(self, message):
        sock = self._connect()
        sock.send(message)
        sock.shutdown(socket.SHUT_WR)
        try:
            result = sock.recv(4096)
            self.assert_(len(result) < 4096)
            return result
        except socket.error, error:
            if error.errno != errno.ECONNRESET:
                raise error
            return ''
        finally:
            sock.close()

    def testReleaseServer(self):
        os.mkdir(MEDIA_PATH + '/release/test-app/dir')
        open(MEDIA_PATH + '/release/test-app/dir/file', 'w').write('hello')

        process = _popen([PATSAK_PATH,
                          '--config-file', CONFIG_PATH,
                          'serve', SOCKET_PATH,
                          APP_NAME])
        self.assertEqual(process.stdout.read(), 'READY\n')
        self.assertEqual(process.wait(), 0)

        sock = self._connect()
        sock.close()

        self.assertEqual(self._talk('UNKNOWN OPERATION'), '')
        self.assertEqual(self._talk('HNDL\nhello'), 'hello')
        self.assertEqual(self._talk('HNDL\nhello'), 'hello')
        self.assertEqual(self._talk('EVAL\nmain()'), 'OK\n0')
        self.assertEqual(self._talk('EVAL\nnew Binary(3)'), 'OK\n\0\0\0')
        self.assert_(
            'Uncaught 42\n    at main.js:' in self._talk('EVAL\nthrow42()'))

        self.assert_(
            'Uncaught 1' in
            self._talk(
                'EVAL\n_core.db.create("xxx", {}, [], [], []); throw 1'))
        self.assertEqual(
            self._talk('EVAL\n'
                       '_core.db.create("xxx", {}, [], [], []);'
                       '_core.db.rollback()'),
            'OK\nundefined')
        self.assertEqual(self._talk('EVAL\n"xxx" in _core.db.list()'),
                         'OK\nfalse')

        # Timed out test. Long to run.
#         self.assertEqual(self._talk('EVAL\nfor (;;) main();'),
#                          'ERROR\n<Timed out>')

        sock = self._connect()
        sock.send('EVAL\n')
        time.sleep(0.1)
        sock.send('2+2')
        sock.shutdown(socket.SHUT_WR)
        self.assertEqual(sock.recv(4096), 'OK\n4')
        sock.close()

        sock = self._connect()
        sock.send('EVAL\n')
        sock.close()

        self.assertEqual(self._talk('EVAL\n_core.spot'), 'OK\nundefined')

        self.assertEqual(self._talk('STOP\n'), '')
        self.assertRaises(socket.error, self._connect)

    def testSpotServer(self):
        process = _popen([PATSAK_PATH,
                          '--config-file', CONFIG_PATH,
                          'serve', SOCKET_PATH,
                          APP_NAME, USER_NAME, SPOT_NAME])
        self.assertEqual(process.stdout.read(), 'READY\n')
        self.assertEqual(process.wait(), 0);

        self.assertEqual(self._talk('HNDL\n1'), '')
        self.assertEqual(self._talk('EVAL\npass'), 'OK\ntrue')
        self.assertEqual(self._talk('EVAL\n_core.owner + " " + _core.spot'),
                         'OK\ntest user test-spot')
        self.assertEqual(self._talk('EVAL\nfunction f() { f(); } f()'),
                         'ERROR\nRangeError: Maximum call stack size exceeded')
        self.assertEqual(self._talk('EVAL\ns="x"; for (;;) s+=s;'),
                         'ERROR\n<Out of memory>')
        self.assertRaises(socket.error, self._connect)


def _create_schema(cursor, schema_name):
    cursor.execute('DROP SCHEMA IF EXISTS "%s" CASCADE' % schema_name)
    cursor.execute('SELECT ak.create_schema(%s)', (schema_name,))


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
    cursor.execute(open(INIT_DB_PATH).read())
    cursor.execute(open(FUNCS_PATH).read())
    for app_name in os.listdir(RELEASE_PATH):
        if (app_name[0] == '.' or
            not os.path.isdir(os.path.join(RELEASE_PATH, app_name))):
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


def _make_dirs():
    if os.path.exists(TMP_PATH):
        shutil.rmtree(TMP_PATH)
    os.makedirs(os.path.join(MEDIA_PATH, 'release', APP_NAME));
    os.makedirs(os.path.join(MEDIA_PATH, 'release', 'another-app'));
    os.makedirs(os.path.join(MEDIA_PATH, 'spots', APP_NAME, USER_NAME))


def _write_config():
    with open(CONFIG_PATH, 'w') as f:
        f.write('''
db-name=%s
db-user=%s
db-password=%s
code-dir=%s/../sample
media-dir=%s
log-file=%s
''' % (DB_NAME, DB_USER, DB_PASSWORD,
       os.path.abspath(TEST_PATH), MEDIA_PATH, LOG_PATH))


def main():
    if len(sys.argv) != 2:
        print 'Usage: ', sys.argv[0], ' dir'
        sys.exit(1)
    global PATSAK_PATH, TEST_PATSAK_PATH
    PATSAK_PATH = os.path.join(sys.argv[1], 'patsak')
    TEST_PATSAK_PATH = os.path.join(sys.argv[1], 'test-patsak')
    suite = unittest.TestLoader().loadTestsFromTestCase(Test)
    _drop_db()
    _create_db()
    _make_dirs()
    _write_config()

    unittest.TextTestRunner(verbosity=2).run(suite)

    _popen(['killall', '-w', 'patsak'])
    shutil.rmtree(TMP_PATH)


if __name__ == '__main__':
    main()
