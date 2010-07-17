#!/usr/bin/env/python

# (c) 2009-2010 by Anton Korenyushkin

from __future__ import with_statement
import subprocess
import os.path
import unittest
import sys
import socket
import os
import shutil
import psycopg2
import errno
import signal


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
HOST         = 'localhost'
PORT1        = 13423
PORT2        = 13424


def _popen(cmd):
    return subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE)


def _launch(args):
    return _popen([PATSAK_PATH, '--config-file', CONFIG_PATH] + args)


class Test(unittest.TestCase):
    def testDatabase(self):
        self.assertEqual(_popen(TEST_PATSAK_PATH).wait(), 0)

    def _check_launch(self, args, code=0):
        process = _launch(args)
        self.assertEqual(process.wait(), code)

    def testMain(self):
        self._check_launch([], 1)
        self._check_launch(['--unknown-option'], 1)
        self._check_launch(['unknown-command'], 1)
        self._check_launch(['--help'])
        self._check_launch(['--rev'])
        self._check_launch(['--code-dir', '', 'serve', 'socket'], 1)
        self._check_launch(['--git-pattern', 'bad', 'eval', '1'], 1)
        self._check_launch(['--daemonize', 'serve', 'bad/path'])
        self._check_launch(['serve', 'bad:bad'], 1)
        self._check_launch(['serve', 'example.com:80'], 1)

    def _eval(self, expr):
        process = _launch(['eval', expr])
        return process.stdout.read()[:-1]

    def testEval(self):
        self.assertEqual(self._eval('test()'), '0')
        self.assertEqual(self._eval('new Binary(3)'), '\0\0\0')
        self.assert_('SyntaxError' in self._eval('argh!!!!'))
        self.assert_('ReferenceError' in self._eval('pish'))
        self.assert_('Uncaught 42\n    at main.js:' in self._eval('throw42()'))
        self.assertEqual(
            self._eval('function f() { f(); } f()'),
            'RangeError: Maximum call stack size exceeded')
        self.assertEqual(self._eval('s="x"; while(1) s+=s'), '<Out of memory>')
        self.assert_(
            'Uncaught 42' in
            self._eval('db.create("xxx", {}, [], [], []); throw 42'))
        self.assertEqual(
            self._eval('db.create("xxx", {}, [], [], []); db.rollback()'),
            'undefined')
        self.assertEqual(self._eval('db.list().indexOf("xxx")'), '-1')
        # Timed out test. Long to run.
#         self.assertEqual(self._eval('for (;;) test();'), '<Timed out>')
        process = _launch(['--code-dir', CODE_PATH + '/syntax', 'eval', '1'])
        self.assert_('SyntaxError' in process.stdout.read())
        self.assertEqual(process.wait(), 0)
        process = _launch(['--code-dir', CODE_PATH + '/eval', 'eval', '1'])
        self.assertEqual(process.stdout.read(), 'eval is not a function\n')
        self.assertEqual(process.wait(), 0)

    def _talk(self, sock, message):
        sock.send(message)
        sock.shutdown(socket.SHUT_WR)
        try:
            return sock.recv(1024)
        finally:
            sock.close()

    def testServe(self):
        process = _launch(['--daemonize', 'serve', SOCKET_PATH])
        self.assertEqual(
            process.stdout.read(), 'Running at unix:%s\n' % SOCKET_PATH)
        self.assertEqual(process.wait(), 0)
        for expr, result in [('x=3; s="x"; while(1) s+=s', ''),
                             ('x = 1', '1'),
                             ('x = 2', '2'),
                             ('y = 0; this.x', 'undefined'),
                             ('x', '1'),
                             ('x', '2'),
                             ('y', '0'),
                             ('throw 1', '')]:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.connect(SOCKET_PATH)
            self.assertEqual(self._talk(sock, expr), result)
        process = _launch(['serve', '%s:%d' % (HOST, PORT1)])
        self.assertEqual(
            process.stdout.readline(), 'Running at %s:%d\n' % (HOST, PORT1))
        self.assertEqual(process.stdout.readline(), 'Quit with Control-C.\n')
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((HOST, PORT1))
        self.assertEqual(self._talk(sock, '"hello"'), 'hello')
        process.send_signal(signal.SIGTERM)
        self.assertEqual(process.wait(), 0)
        process = _launch(['serve', str(PORT2)])
        self.assertEqual(
            process.stdout.readline(), 'Running at 127.0.0.1:%d\n' % PORT2)
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect(('127.0.0.1', PORT2))
        self.assertEqual(self._talk(sock, '"hello"'), 'hello')
        process.kill()


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
db-name=%s
db-user=%s
db-password=%s
code-dir=%s/main
lib-dir=%s
media-dir=%s
git-pattern=%s/%%s/.git
log-file=%s
workers=3
''' % (DB_NAME, DB_USER, DB_PASSWORD,
       CODE_PATH, LIB_PATH, MEDIA_PATH, CODE_PATH, LOG_PATH))

    unittest.TextTestRunner(verbosity=2).run(suite)

    _popen(['killall', 'patsak'])
    shutil.rmtree(TMP_PATH)


if __name__ == '__main__':
    main()
