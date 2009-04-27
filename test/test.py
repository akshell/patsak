#!/usr/bin/env/python

# (c) 2009 by Anton Korenyushkin

''' Python test entry point '''

import subprocess
import os.path
import unittest
import sys
import socket
import time
import os
import shutil
import psycopg2


TEST_EXE_NAME      = 'test-patsak'
EXE_NAME           = 'patsak'
TMP_DIR            = '/tmp/patsak'
SOCKET_DIR         = os.path.join(TMP_DIR, 'sockets')
LOG_DIR            = os.path.join(TMP_DIR, 'logs')
APP_NAME           = 'test_app'
USER_NAME          = 'test_user'
TAG_NAME           = 'test_tag'
CONFIG_PATH        = 'patsak.config'


class EvalResult:
    def __init__(self, status, data):
        self.status = status
        self.data = data
        
        
def _create_db(cursor, db_name):
    try:
        cursor.execute('CREATE DATABASE "%s" '
                       'WITH OWNER "patsak" '
                       'TEMPLATE "template";' % db_name)
    except psycopg2.Error, error:
        if error.pgcode != '42P04':
            raise
        

class Test(unittest.TestCase):
    def setUp(self):
        if os.path.exists(TMP_DIR):
            shutil.rmtree(TMP_DIR)
        self._exe_path = os.path.join(Test.DIR, EXE_NAME)
        self._test_exe_path = os.path.join(Test.DIR, TEST_EXE_NAME)
        
        self._in = open('/dev/null', 'r')
        self._out = open('/dev/null', 'w')

        self._release_socket_dir = os.path.join(SOCKET_DIR, 'release')
        os.makedirs(self._release_socket_dir)
        self._tag_socket_dir = os.path.join(SOCKET_DIR, 'tags',
                                            APP_NAME, USER_NAME)
        os.makedirs(self._tag_socket_dir)

        self._release_log_dir = os.path.join(LOG_DIR, 'release')
        os.makedirs(self._release_log_dir)
        self._tag_log_dir = os.path.join(LOG_DIR, 'tags',
                                         APP_NAME, USER_NAME)
        os.makedirs(self._tag_log_dir)
        conn = psycopg2.connect("user=test password=test dbname=template")
        conn.set_isolation_level(0)
        cursor = conn.cursor()
        _create_db(cursor, "test_patsak")
        _create_db(cursor, "test_patsak_test_app")

        

    def tearDown(self):
        self._out.close()
        self._in.close()
        shutil.rmtree(TMP_DIR)
        
    def _check_launch(self, args, code=0, program=None):
        popen = subprocess.Popen([program if program else self._exe_path,
                                  '--config-file', CONFIG_PATH] + args,
                                 stdin=self._in,
                                 stdout=self._out,
                                 stderr=self._out)
        self.assertEqual(popen.wait(), code)

    def testTestPatsak(self):
        self._check_launch([], 0, self._test_exe_path)

        
    def testMain(self):
        self._check_launch([], 1)
        self._check_launch(['--unknown-option', APP_NAME], 1)
        self._check_launch(['--help'])
        self._check_launch(['--eval', '2+2',
                            '--socket-dir', SOCKET_DIR,
                            '--log-dir', LOG_DIR,
                            APP_NAME],
                           1)
        self._check_launch(['--test'], 1)
        self._check_launch(['--test', APP_NAME, USER_NAME], 1)

    def _eval(self, expr):
        popen = subprocess.Popen([self._exe_path,
                                  '--config-file', CONFIG_PATH,
                                  '--test',
                                  '--eval', expr,
                                  APP_NAME],
                                 stdin=self._in,
                                 stderr=self._out,
                                 stdout=subprocess.PIPE)
        status = popen.stdout.readline()[:-1]
        return EvalResult(status, popen.stdout.read()[:-1])

    def testJS(self):
        self.assertEqual(self._eval('argh!!!!').status, 'ERROR')
        pish_result = self._eval('pish')
        self.assertEqual(pish_result.status, 'ERROR')
        self.assertEqual(pish_result.data,
                         'EXCEPTION Uncaught ReferenceError: '
                         'pish is not defined\n'
                         'LINE 1\n'
                         'COLUMN 0\n')
        self.assertEqual(self._eval('main()').data, '0')
        self._check_launch(['--test',
                            '--eval', '2+2',
                            'no_such_app'], 1)
        self._check_launch(['--test',
                            '--eval', '2+2\n3+3',
                            APP_NAME])
        self.assertEqual(self._eval('bug()').status, 'ERROR')

        popen = subprocess.Popen([self._exe_path,
                                  '--config-file', CONFIG_PATH,
                                  '--test',
                                  APP_NAME],
                                 stdin=subprocess.PIPE,
                                 stderr=self._out,
                                 stdout=subprocess.PIPE)
        popen.stdin.write('2+2\n')
        popen.stdin.close()
        self.assertEqual(popen.stdout.read(), 'OK\n4\n')
        self.assertEqual(popen.wait(), 0)

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
        popen = subprocess.Popen([self._exe_path,
                                  '--config-file', CONFIG_PATH,
                                  '--socket-dir', SOCKET_DIR,
                                  '--log-dir', LOG_DIR,
                                  APP_NAME],
                                 stdin=self._in,
                                 stderr=self._out,
                                 stdout=subprocess.PIPE)
        
        self.assertEqual(popen.stdout.readline(), 'READY\n')

        socket_path = os.path.join(self._release_socket_dir, APP_NAME)
        
        sock = self._connect(socket_path)
        sock.close()
        sock = self._connect(socket_path)
        sock.send('STATUS\n')
        sock.close()
        
        self.assertEqual(self._talk_through_socket(socket_path, 'STATUS'),
                         'OK\n')
        
        self.assertEqual(self._talk_through_socket(socket_path, 'EVAL 2+2'),
                         'OK\n4')
        self.assertEqual(self._talk_through_socket(socket_path,
                                                   'EVAL main(); 2+2'),
                         'OK\n4')
        self.assertEqual(self._talk_through_socket(socket_path,
                                                   'EVAL\nEXPR 3\n2+2'),
                         'OK\n4')
        self.assertEqual(self._talk_through_socket(socket_path, 'EVAL ak._data'),
                         'OK\nnull')
        self.assertEqual(self._talk_through_socket(socket_path,
                                                   'EVAL\nEXPR 8\nak._data'),
                         'OK\nnull')
        self.assertEqual(self._talk_through_socket(socket_path,
                                                   'EVAL\nDATA 5\nhello\n'
                                                   'EXPR 11\nak._data+""'),
                         'OK\nhello')
        
        self.assertEqual(self._talk_through_socket(socket_path, 'UNKNOWN'),
                         'FAIL\nUnknown request: UNKNOWN')
        self.assertEqual(self._talk_through_socket(socket_path,
                                                   'EVAL\nEXPR 3 hi!\n2+2'),
                         'FAIL\nIll formed request')
        self.assertEqual(self._talk_through_socket(socket_path,
                                                   'EVAL\nSTRANGE_CMD\n'),
                         'FAIL\nUnexpected command: STRANGE_CMD')
        self.assertEqual(self._talk_through_socket(socket_path, 'STOP'),
                         'OK\n')
        self.assertEqual(popen.wait(), 0)

    def testTagServer(self):
        popen = subprocess.Popen([self._exe_path,
                                  '--config-file', CONFIG_PATH,
                                  '--socket-dir', SOCKET_DIR,
                                  '--log-dir', LOG_DIR,
                                  APP_NAME, USER_NAME, TAG_NAME],
                                 stdin=self._in,
                                 stderr=self._out,
                                 stdout=subprocess.PIPE)
        
        self.assertEqual(popen.stdout.readline(), 'READY\n')
        socket_path = os.path.join(self._tag_socket_dir, TAG_NAME)
        self.assertEqual(self._talk_through_socket(socket_path, 'EVAL main()'),
                         'OK\n0')
        self.assertEqual(self._talk_through_socket(socket_path, 'STOP'),
                         'OK\n')
        self.assertEqual(popen.wait(), 0)
        

def main():
    if len(sys.argv) != 2:
        print 'Usage: ', sys.argv[0], ' dir'
        sys.exit(1)
    Test.DIR = sys.argv[1]
    suite = unittest.TestLoader().loadTestsFromTestCase(Test)
    unittest.TextTestRunner(verbosity=2).run(suite)
    
        
if __name__ == '__main__':
    main()
