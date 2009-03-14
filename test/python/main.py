#!/usr/bin/env/python

# (c) 2009 by Anton Korenyushkin

''' Python test entry point '''

import subprocess
import os.path
import unittest
import sys
import socket
import time


KU_TEST_FILE_NAME  = 'test-ku'
KU_FILE_NAME       = 'ku'
JS_MAIN_PATH       = 'test/js/main.js'
JS_HELLO_PATH      = 'test/js/hello.js'
JS_UNREADABLE_PATH = 'test/js/unreadable.js'
DB_OPTIONS         = 'dbname=test'
SOCKET_DIR         = '/tmp'
NAME               = 'test_app1'


class EvalResult:

    def __init__(self, status, data):
        self.status = status
        self.data = data
        

class Test(unittest.TestCase):
    
    def setUp(self):
        self._ku_path = os.path.join(Test.DIR, KU_FILE_NAME)
        self._ku_test_path = os.path.join(Test.DIR, KU_TEST_FILE_NAME)
        self._socket_path = os.path.join(SOCKET_DIR, NAME)
        self._in = open('/dev/null', 'r')
        self._out = open('/dev/null', 'w')

    def tearDown(self):
        self._out.close()
        self._in.close()
        
    def _check_launch(self, args, code=0, program=None):
        popen = subprocess.Popen([program if program else self._ku_path] + args,
                                 stdin=self._in,
                                 stdout=self._out,
                                 stderr=self._out)
        self.assertEqual(popen.wait(), code)

    def testKuTest(self):
        self._check_launch([], 0, self._ku_test_path)

        
    def testMain(self):
        self._check_launch([], 1)
        self._check_launch(['--server', JS_MAIN_PATH], 1)
        self._check_launch(['--test', '--db-options', DB_OPTIONS, JS_MAIN_PATH])
        self._check_launch(['--test',
                            '--db-options', 'dbname=illegal',
                            JS_MAIN_PATH],
                           1)
        self._check_launch(['-h'])
        self._check_launch(['--illegal'], 1)
        self._check_launch(['--server',
                            '--eval', '2+2',
                            JS_MAIN_PATH], 1)
        self._check_launch(['--test',
                            '--eval', '2+2',
                            '--test-file',
                            JS_HELLO_PATH,
                            JS_MAIN_PATH], 1)
        self._check_launch(['--test',
                            '--test-file', 'no-such-file.js',
                            JS_MAIN_PATH], 1)
        self._check_launch(['--test',
                            '--test-file', JS_HELLO_PATH,
                            '--db-options', DB_OPTIONS,
                            JS_MAIN_PATH])        

    def _eval(self, expr):
        popen = subprocess.Popen([self._ku_path,
                                  '--test',
                                  '--db-options', DB_OPTIONS,
                                  '--eval', expr,
                                  JS_MAIN_PATH],
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
                            '--db-options', DB_OPTIONS,
                            'no-such-dir/main.js'], 1)
        self._check_launch(['--test',
                            '--eval', '2+2',
                            '--db-options', DB_OPTIONS,
                            '--log-file', '/dev/null',
                            'no-such-file.js'], 1)
        self._check_launch(['--test',
                            '--eval', '2+2\n3+3',
                            '--db-options', DB_OPTIONS,
                            JS_MAIN_PATH])
        self._check_launch(['--test',
                            '--eval', '2+2',
                            '--db-options', DB_OPTIONS,
                            '--name', NAME,
                            JS_MAIN_PATH], 1)
        self.assertEqual(
            subprocess.Popen([self._ku_path,
                              '--test',
                              '--eval', '2+2',
                              '--db-options', DB_OPTIONS,
                              JS_UNREADABLE_PATH],
                             stdin=self._in,
                             stderr=self._out,
                             stdout=subprocess.PIPE).stdout.readline(),
            'ERROR\n')
        self.assertEqual(self._eval('bug()').status, 'ERROR')

    def _connect(self):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(self._socket_path)
        return sock

    def _talk_through_socket(self, message):
        sock = self._connect()
        sock.send(message + '\n')
        result = sock.recv(4096)
        sock.close()
        self.assert_(len(result) < 4096)
        return result

    def testServer(self):
        popen = subprocess.Popen([self._ku_path,
                                  '--server',
                                  '--db-options', DB_OPTIONS,
                                  '--socket-dir', SOCKET_DIR,
                                  '--name', NAME,
                                  '-l', 'log',
                                  JS_MAIN_PATH],
                                 stdin=self._in,
                                 stderr=self._out,
                                 stdout=subprocess.PIPE)
        
        self.assertEqual(popen.stdout.readline(), "READY\n")

        sock = self._connect()
        sock.close()
        sock = self._connect()
        sock.send('STATUS\n')
        sock.close()
        
        self.assertEqual(self._talk_through_socket('STATUS'),
                         'OK')
        self.assertEqual(self._talk_through_socket('UNKNOWN'),
                         'FAIL')
        self.assertEqual(self._talk_through_socket('EVAL 2+2'),
                         'OK 4')
        self.assertEqual(self._talk_through_socket('EVAL main(); 2+2'),
                         'OK 4')
        self.assertEqual(self._talk_through_socket('STOP'),
                         'OK')
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
