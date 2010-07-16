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


DB_NAME      = 'test'
DB_USER      = 'test'
DB_PASSWORD  = 'test'
TMP_PATH     = '/tmp/patsak'
SOCKET_PATH  = TMP_PATH + '/socket'
MEDIA_PATH   = TMP_PATH + '/media'
CONFIG_PATH  = TMP_PATH + '/config'
LOG_PATH     = TMP_PATH + '/log'
TEST_PATH    = os.path.dirname(__file__)
CODE_PATH    = TEST_PATH + '/code'
LIB_PATH     = TEST_PATH + '/../lib'
FUNCS_PATH   = TEST_PATH + '/../src/funcs.sql'


def _popen(cmd):
    return subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE)


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
        self._check_launch(['unknown-command'], 1)
        self._check_launch(['--help'])
        self._check_launch(['--rev'])
        self._check_launch(['serve'], 1)
        self._check_launch(['--code-dir', '', 'serve', 'socket'], 1)
        self._check_launch(['--git-pattern', 'bad', 'eval', '1'], 1)
        self._check_launch(['serve', 'bad/path'])

    def _eval(self, expr):
        process = _popen(
            [PATSAK_PATH, '--config-file', CONFIG_PATH, 'eval', expr])
        return process.stdout.read()[:-1]

    def testJS(self):
        self.assert_('SyntaxError' in self._eval('argh!!!!'))
        self.assert_('ReferenceError' in self._eval('pish'))
        self.assertEqual(self._eval('2+2'), 'OK\n4')
        self.assertEqual(self._eval('2+2\n3+3'), 'OK\n6')
        self.assertEqual(self._eval('s="x"; while(1) s+=s'),
                         'ERROR\n<Out of memory>')
        self._check_launch(['eval', '2+2\n3+3'])
        process = _popen(
            [PATSAK_PATH,
             '--config-file', CONFIG_PATH,
             '--code-dir', CODE_PATH + '/bad',
             'eval', '1'])
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
        process = _popen(
            [PATSAK_PATH, '--config-file', CONFIG_PATH, 'serve', SOCKET_PATH])
        self.assertEqual(process.stdout.read(), 'READY\n')
        self.assertEqual(process.wait(), 0)

        sock = self._connect()
        sock.close()

        self.assertEqual(self._talk('UNKNOWN OPERATION'), '')
        self.assertEqual(self._talk('HNDL\nhello'), 'hello')
        self.assertEqual(self._talk('HNDL\nhello'), 'hello')
        self.assertEqual(self._talk('EVAL\ntest()'), 'OK\n0')
        self.assertEqual(self._talk('EVAL\nnew Binary(3)'), 'OK\n\0\0\0')
        self.assert_(
            'Uncaught 42\n    at main.js:' in self._talk('EVAL\nthrow42()'))
        self.assertEqual(self._talk('EVAL\nfunction f() { f(); } f()'),
                         'ERROR\nRangeError: Maximum call stack size exceeded')

        self.assert_(
            'Uncaught 1' in
            self._talk(
                'EVAL\ndb.create("xxx", {}, [], [], []); throw 1'))
        self.assertEqual(
            self._talk('EVAL\n'
                       'db.create("xxx", {}, [], [], []);'
                       'db.rollback()'),
            'OK\nundefined')
        self.assertEqual(self._talk('EVAL\n"xxx" in db.list()'),
                         'OK\nfalse')

        # Timed out test. Long to run.
#         self.assertEqual(self._talk('EVAL\nfor (;;) test();'),
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

        self.assertEqual(self._talk('STOP\n'), '')
        self.assertRaises(socket.error, self._connect)


def _connect_to_db(name):
    return psycopg2.connect(
        'user=%s password=%s dbname=%s' % (DB_USER, DB_PASSWORD, name))


def main():
    if len(sys.argv) != 2:
        print 'Usage: ', sys.argv[0], ' dir'
        sys.exit(1)

    global PATSAK_PATH, TEST_PATSAK_PATH
    PATSAK_PATH = os.path.join(sys.argv[1], 'patsak')
    TEST_PATSAK_PATH = os.path.join(sys.argv[1], 'test-patsak')
    suite = unittest.TestLoader().loadTestsFromTestCase(Test)

    conn = _connect_to_db('template1')
    conn.set_isolation_level(0)
    cursor = conn.cursor()
    try:
        cursor.execute('DROP DATABASE "%s"' % DB_NAME)
    except psycopg2.ProgrammingError, error:
        if error.pgcode != '3D000':
            raise
    cursor.execute('CREATE DATABASE "%s"' % DB_NAME)
    conn.close()
    conn = _connect_to_db(DB_NAME)
    cursor = conn.cursor()
    cursor.execute(open(FUNCS_PATH).read())
    conn.commit()
    conn.close()

    try:
        os.makedirs(MEDIA_PATH)
    except OSError, error:
        if error.errno != errno.EEXIST:
            raise

    with open(CONFIG_PATH, 'w') as f:
        f.write('''
daemonize=1
db-name=%s
db-user=%s
db-password=%s
code-dir=%s/test
lib-dir=%s
media-dir=%s
git-pattern=%s/%%s/.git
log-file=%s
''' % (DB_NAME, DB_USER, DB_PASSWORD,
       CODE_PATH, LIB_PATH, MEDIA_PATH, CODE_PATH, LOG_PATH))

    unittest.TextTestRunner(verbosity=2).run(suite)

    _popen(['killall', '-w', 'patsak'])
    shutil.rmtree(TMP_PATH)


if __name__ == '__main__':
    main()
