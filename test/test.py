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


TEST_EXE_NAME = 'test-patsak'
EXE_NAME      = 'patsak'
TMP_DIR       = '/tmp/patsak'
SOCKET_DIR    = os.path.join(TMP_DIR, 'sockets')
LOG_DIR       = os.path.join(TMP_DIR, 'logs')
GUARD_DIR     = os.path.join(TMP_DIR, 'guards')
MEDIA_DIR     = os.path.join(TMP_DIR, 'media')
APP_NAME      = 'test_app'
BAD_APP_NAME  = 'bad_app'
USER_NAME     = 'test_user'
SPOT_NAME     = 'test_spot'
CONFIG_PATH   = 'patsak.config'
DB_NAME       = 'test_patsak'
DB_PARAMS     = 'user=patsak password=1q2w3e dbname=%s'
TEST_DIR      = os.path.dirname(__file__)
RELEASE_DIR   = os.path.join(TEST_DIR, '../sample/code/release')
INIT_DB_PATH  = os.path.join(TEST_DIR, 'init-db.sql')
AK_FUNCS_PATH = os.path.join(TEST_DIR, '../src/ak-funcs.sql')


class _Response:
    def __init__(self, status, data):
        self.status = status
        self.data = data


class Test(unittest.TestCase):
    def setUp(self):
        self._exe_path = os.path.join(Test.DIR, EXE_NAME)
        self._test_exe_path = os.path.join(Test.DIR, TEST_EXE_NAME)
        
        self._in = open('/dev/null', 'r')
        self._out = open('/dev/null', 'w')

    def tearDown(self):
        self._out.close()
        self._in.close()
        
    def _check_launch(self, args, code=0, program=None):
        popen = subprocess.Popen([program if program else self._exe_path,
                                  '--config-file', CONFIG_PATH] + args,
                                 stdin=self._in,
                                 stdout=self._out,
                                 stderr=self._out)
        self.assertEqual(popen.wait(), code)

    def _check_test_launch(self, args, code=0, program=None):
        self._check_launch(['--media-dir', MEDIA_DIR,
                            '--socket-dir', SOCKET_DIR,
                            '--guard-dir', GUARD_DIR,
                            '--log-dir', LOG_DIR,
                            '--test'] + args,
                           code,
                           program)

    def testTestPatsak(self):
        self._check_launch([], 0, self._test_exe_path)

        
    def testMain(self):
        self._check_launch([], 1)
        self._check_launch(['--unknown-option', APP_NAME], 1)
        self._check_launch(['--help'])
        self._check_launch(['--expr', '2+2',
                            '--socket-dir', SOCKET_DIR,
                            '--guard-dir', GUARD_DIR,
                            '--log-dir', LOG_DIR,
                            '--media-dir', MEDIA_DIR,
                            APP_NAME],
                           1)
        self._check_test_launch([], 1)
        self._check_test_launch([APP_NAME, USER_NAME], 1)

    def _eval(self, expr):
        popen = subprocess.Popen([self._exe_path,
                                  '--config-file', CONFIG_PATH,
                                  '--media-dir', MEDIA_DIR,
                                  '--socket-dir', SOCKET_DIR,
                                  '--guard-dir', GUARD_DIR,
                                  '--log-dir', LOG_DIR,
                                  '--test',
                                  '--expr', expr,
                                  APP_NAME],
                                 stdin=self._in,
                                 stderr=self._out,
                                 stdout=subprocess.PIPE)
        status = popen.stdout.readline()[:-1]
        return _Response(status, popen.stdout.read()[:-1])

    def testJS(self):
        self.assertEqual(self._eval('argh!!!!').status, 'ERROR')
        pish_result = self._eval('pish')
        self.assertEqual(pish_result.status, 'ERROR')
        self.assertEqual(pish_result.data,
                         'Line 1, column 0\n'
                         'ReferenceError: pish is not defined\n'
                         '    at eval at eval (native)\n'
                         '    at eval (native)')
        self.assertEqual(self._eval('2+2').data, '4')
        self.assertEqual(self._eval('s="x"; while(1) s+=s').data,
                         '<Out of memory>')
        self._check_launch(['--test',
                            '--media-dir', MEDIA_DIR,
                            '--expr', '2+2',
                            'no_such_app'], 1)
        self._check_test_launch(['--expr', '2+2\n3+3', APP_NAME])
        self.assertEqual(self._eval('bug()').status, 'ERROR')

        popen = subprocess.Popen([self._exe_path,
                                  '--config-file', CONFIG_PATH,
                                  '--media-dir', MEDIA_DIR,
                                  '--socket-dir', SOCKET_DIR,
                                  '--guard-dir', GUARD_DIR,
                                  '--log-dir', LOG_DIR,
                                  '--test',
                                  APP_NAME],
                                 stdin=subprocess.PIPE,
                                 stderr=self._out,
                                 stdout=subprocess.PIPE)
        popen.stdin.write('2+2\n')
        popen.stdin.close()
        self.assertEqual(popen.stdout.read(), 'OK\n4\n')
        self.assertEqual(popen.wait(), 0)

        popen = subprocess.Popen([self._exe_path,
                                  '--config-file', CONFIG_PATH,
                                  '--media-dir', MEDIA_DIR,
                                  '--socket-dir', SOCKET_DIR,
                                  '--guard-dir', GUARD_DIR,
                                  '--log-dir', LOG_DIR,
                                  '--test',
                                  '--expr', '2+2',
                                  BAD_APP_NAME],
                                 stdin=self._in,
                                 stderr=self._out,
                                 stdout=subprocess.PIPE)
        self.assertEqual(popen.stdout.read().split('\n')[0], 'ERROR')
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
        # populating media dir with initial data to test FSBg initialization
        os.mkdir(MEDIA_DIR + '/release/test_app/dir')
        open(MEDIA_DIR + '/release/test_app/dir/file', 'w').write('hello')
        
        popen = subprocess.Popen([self._exe_path,
                                  '--config-file', CONFIG_PATH,
                                  '--socket-dir', SOCKET_DIR,
                                  '--guard-dir', GUARD_DIR,
                                  '--log-dir', LOG_DIR,
                                  '--media-dir', MEDIA_DIR,
                                  APP_NAME],
                                 stdin=self._in,
                                 stderr=self._out,
                                 stdout=subprocess.PIPE)
        
        self.assertEqual(popen.stdout.readline(), 'READY\n')

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
        self.assertEqual(talk('PROCESS ak._data'), 'OK\nnull')
        self.assertEqual(talk('PROCESS\nREQUEST 8\nak._data'), 'OK\nnull')
        self.assertEqual(talk('PROCESS\nDATA 5\nhello\n'
                              'REQUEST 11\nak._data+""'),
                         'OK\nhello')
        wuzzup_path = os.path.join(TMP_DIR, 'wuzzup.txt')
        open(wuzzup_path, 'w').write('wuzzup!!1')
        self.assertEqual(talk('PROCESS\n\nFILE /does_not_exist\n'
                              'FILE ' + wuzzup_path + '\n'
                              'REQUEST 25\nak.fs._read(ak._files[1])'),
                         'OK\nwuzzup!!1')
        self.assert_(os.path.exists(wuzzup_path))
        self.assert_('Temp file is already removed' in
                     talk('PROCESS\nFILE ' + wuzzup_path + '\n' +
                          'REQUEST 53\n'
                          'ak.fs._remove(ak._files[0]);'
                          'ak.fs._read(ak._files[0])'))
        self.assert_(not os.path.exists(wuzzup_path))

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
        self.assertEqual(talk('PROCESS\nISSUER test_app\nEXPR 3\n2+2'),
                         'FAIL\nISSUER is not supported by EXPR')
        
        self.assertEqual(talk('PROCESS ak.app.spot'),
                         'OK\nundefined')
        self.assertEqual(talk('STOP'), 'OK\n')
        self.assertEqual(popen.wait(), 0)

    def testSpotServer(self):
        popen = subprocess.Popen([self._exe_path,
                                  '--config-file', CONFIG_PATH,
                                  '--socket-dir', SOCKET_DIR,
                                  '--guard-dir', GUARD_DIR,
                                  '--log-dir', LOG_DIR,
                                  '--media-dir', MEDIA_DIR,
                                  APP_NAME, USER_NAME, SPOT_NAME],
                                 stdin=self._in,
                                 stderr=self._out,
                                 stdout=subprocess.PIPE)
        
        self.assertEqual(popen.stdout.readline(), 'READY\n')
        socket_path = os.path.join(SOCKET_DIR, 'spots',
                                   APP_NAME, USER_NAME, SPOT_NAME)
        def talk(message):
            return self._talk_through_socket(socket_path, message)
        
        self.assertEqual(talk('PROCESS\nREQUEST 1\n1'),
                         'ERROR\n"_main" is not a function')
        self.assertEqual(talk('PROCESS answer'), 'OK\n42')
        self.assertEqual(
            talk('PROCESS ak.app.spot.owner + " " + ak.app.spot.name'),
            'OK\ntest_user test_spot')
        self.assertEqual(talk('PROCESS s="x"; while(1) s+=s'),
                         'ERROR\n<Out of memory>')
        self.assertEqual(popen.wait(), 0)

        
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
    cursor.execute(open(INIT_DB_PATH).read())
    cursor.execute(open(AK_FUNCS_PATH).read())
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
    _make_dir_tree(LOG_DIR)
    _make_dir_tree(GUARD_DIR)
    _make_dir_tree(MEDIA_DIR)
    os.mkdir(os.path.join(MEDIA_DIR, 'release', APP_NAME))

    
def main():
    if len(sys.argv) != 2:
        print 'Usage: ', sys.argv[0], ' dir'
        sys.exit(1)
    Test.DIR = sys.argv[1]
    suite = unittest.TestLoader().loadTestsFromTestCase(Test)
    _drop_db()
    _create_db()
    _make_dirs()
    
    unittest.TextTestRunner(verbosity=2).run(suite)
    
    subprocess.Popen('killall -w patsak; killall -w exe',
                     shell=True,
                     stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT)
    shutil.rmtree(TMP_DIR)
    
        
if __name__ == '__main__':
    main()
