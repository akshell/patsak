
// (c) 2009-2010 by Anton Korenyushkin

Script = _core.Script;
Proxy = _core.Proxy;
db = _core.db;
fs = _core.fs;

number = db.number;
string = db.string;
bool = db.bool;
date = db.date;

READ_ONLY   = 1 << 0;
DONT_ENUM   = 1 << 1;
DONT_DELETE = 1 << 2;


[
  'print',
  'readCode',
  'getCodeModDate',
  'set',
  'hash',
  'construct',
  'requestApp',
  'requestHost'
].forEach(
  function (name) {
    var func = _core[name];
    this[name] = function () {
      return func.apply(_core, arguments);
    };
  });


_core.errors.slice(1).forEach(
  function (error) {
    this[error.__name__] = error;
  });


function remove(path) {
  if (fs.isDir(path)) {
    var children = fs.list(path);
    for (var i = 0; i < children.length; ++i)
      arguments.callee(path + '/' + children[i]);
  }
  fs.remove(path);
};


set(_core.Data.prototype,
    'toString',
    DONT_ENUM,
    function (encoding) {
      return this._toString(encoding || 'UTF-8');
    });


function query(query, queryParams, by, byParams, start, length) {
  return db.query(query,
                  queryParams || [],
                  by || [],
                  byParams || [],
                  start || 0,
                  length);
}


function field(name, str, queryParams, by, byParams, start, length) {
  return Array.prototype.map.call(
    query('for (x in ' + str + ') x.' + name,
          queryParams, by, byParams, start, length),
    function (item) { return item[name]; });
}


function create(name, header, constrs) {
  constrs = constrs || {};
  return db.create(name,
                   header,
                   constrs.unique || [],
                   constrs.foreign || [],
                   constrs.check || []);
}


function keys(object) {
  var result = [];
  for (var key in object)
    result.push(key);
  return result;
}


function items(object) {
  var result = [];
  for (var key in object)
    result.push([key, object[key]]);
  return result;
}


function equal(lhs, rhs) {
  if (lhs == rhs)
    return true;
  if (lhs instanceof Date && rhs instanceof Date)
    return lhs.getTime() == rhs.getTime();
  if (lhs instanceof Array && rhs instanceof Array) {
    if (lhs.length != rhs.length)
      return false;
    for (var i = 0; i < lhs.length; ++i)
      if (!equal(lhs[i], rhs[i]))
        return false;
    return true;
  }
  return false;
}


function repr(value) {
  if (typeof(value) == 'string')
    return '"' + value + '"';
  if (value instanceof Array)
    return '[' + value.map(repr).join(', ') + ']';
  return value + '';
}

////////////////////////////////////////////////////////////////////////////////
// Test tools
////////////////////////////////////////////////////////////////////////////////

function assert(value) {
  if (!value)
    throw Error('Assertion failed');
}


function assertSame(lhs, rhs) {
  if (lhs !== rhs)
    throw Error(repr(lhs) + ' !== ' + repr(rhs));
}


function assertEqual(lhs, rhs) {
  if (!equal(lhs, rhs))
    throw Error(repr(lhs) + ' <> ' + repr(rhs));
}


function assertThrow(cls, func) {
  try {
    if (typeof(func) == 'string')
      eval(func);
    else
      func.apply(this, Array.prototype.slice.call(arguments, 2));
  } catch (error) {
    if (error instanceof cls)
      return;
    throw Error('Unexpected exception ' + error);
  }
  throw Error('Function has not throw an exception');
}


function runTestSuites(suites) {
  var errorCount = 0;
  var testCount = 0;
  for (var i = 0; i < suites.length; ++i) {
    var suite = suites[i];
    if (suite.setUp)
      suite.setUp();
    for (var name in suite) {
      if (name.substr(0, 4) == 'test') {
        try {
          suite[name]();
        } catch (error) {
          ++errorCount;
          print(error.stack);
        }
        ++testCount;
      }
    }
    if (suite.tearDown)
      suite.tearDown();
  }
  print(errorCount + ' errors detected in ' +
        testCount + ' test cases in ' +
        suites.length + ' test suites');
  return errorCount;
}

////////////////////////////////////////////////////////////////////////////////
// Base test suite
////////////////////////////////////////////////////////////////////////////////

var global = this;

var baseTestSuite = {
  testRequire: function () {
    [
      'absolute',
      'cyclic',
      'determinism',
      'exactExports',
      'method',
      'missing',
      'monkeys',
      'nested',
      'relative',
      'transitive',
      'module/./../module//a'
    ].forEach(
      function (version) {
        assert(require('lib', version).pass);
      });
    var object = require('subdir/index').object;
    assertSame(require('test-app', '', 'subdir/../subdir//index').object,
               object);
    assertSame(require('test-app', 'subdir/.././subdir', './//./index').object,
               object);
    assertSame(require('test-app', 'subdir').object, object);
    assertEqual(items(module), [['id', 'main'], ['app', 'test-app']]);
    assertThrow(UsageError, require);
  },

  testType: function () {
    assertSame(number.name, 'number');
    assertSame(string.name, 'string');
    assertSame(bool.name, 'bool');
    assertSame(date.name, 'date');
  },

  testConstructors: function () {
    assert(global instanceof _core.Global);
    assert(_core instanceof _core.Core);
    assert(db instanceof _core.DB);
    assert(keys(_core).indexOf('Core') != -1);
    assert(_core.hasOwnProperty('set'));
  },

  testSet: function () {
    var obj = {};
    assertThrow(UsageError, set);
    assertThrow(TypeError, set, 1, 'f', 0, 42);
    assertSame(set(obj, 'readOnly', READ_ONLY, 1), obj);
    set(obj, 'dontEnum', DONT_ENUM, 2);
    set(obj, 'dontDelete', DONT_DELETE, 3);
    assertThrow(TypeError, set, obj, 'field', {}, 42);
    assertThrow(UsageError, set, obj, 'field', 8, 42);
    obj.readOnly = 5;
    assertSame(obj.readOnly, 1);
    assertEqual(keys(obj), ['readOnly', 'dontDelete']);
    delete obj.dontDelete;
    assertSame(obj.dontDelete, 3);
  },

  testApp: function () {
    assertSame(_core.app, 'test-app');
  },

  testReadCode: function () {
    assertSame(readCode('subdir/hi.txt'), 'russian привет\n');
    assertSame(readCode('bad-app', 'main.js'), 'wuzzup!!!!!!!!\n');
    assertThrow(NoSuchAppError, readCode, 'illegal/name', 'main.js');
    assertThrow(PathError, readCode, 'test-app', '');
    assertThrow(PathError,
                readCode, 'test-app', 'subdir/../../another-app/main.js');
    assertThrow(UsageError, readCode);
  },

  testGetCodeModDate: function () {
    assert(getCodeModDate('main.js') > new Date('01.01.2010'));
    assert(getCodeModDate('lib', 'absolute/index.js') < new Date());
    assertThrow(NoSuchEntryError, getCodeModDate, 'no-such-file');
    assertThrow(NoSuchEntryError, getCodeModDate, 'lib', 'no-such-file');
    assertThrow(NoSuchAppError, getCodeModDate, 'no-such-app', 'file');
  },

  testScript: function () {
    assertSame((new Script('2+2'))._run(), 4);
    assertSame(Script('2+2'), undefined);
    assertThrow(UsageError, "new Script()");
    assertThrow(SyntaxError, "new Script('(')");
    assertThrow(ReferenceError, "new Script('undeclarated')._run()");
    assertThrow(
      SyntaxError,
      function () { new Script('new Script("(")', 'just string')._run(); });
    try {
      new Script('undeclarated', 'some name', 10, 20)._run();
      assert(false);
    } catch (error) {
      assert(error instanceof ReferenceError);
      assert(error.stack.indexOf('some name:11:21\n') != -1);
    }
    try {
      new Script('undeclarated', 'some name', 10)._run();
      assert(false);
    } catch (error) {
      assert(error.stack.indexOf('some name:11:1\n') != -1);
    }
    try {
      new Script('undeclarated', 'some name', {}, 20)._run();
      assert(false);
    } catch (error) {
      assert(error.stack.indexOf('some name:1:21\n') != -1);
    }
  },

  testHash: function () {
    assertThrow(UsageError, hash);
    assertSame(hash(undefined), 0);
    assertSame(hash(null), 0);
    assertSame(hash(42), 0);
    assertSame(hash('foo'), 0);
    assertSame(hash(''), 0);
    assert(hash({}) > 0);
    assert(hash(function () {}) > 0);
  },

  testErrors: function () {
    assert(new BaseError() instanceof Error);
    assert(new CoreError() instanceof BaseError);
    assertSame(CoreError.__name__, 'CoreError');
    assertSame(CoreError.prototype.name, 'CoreError');
    assert(new DBError() instanceof CoreError);
    assert(new FieldError() instanceof DBError);
    assert(UsageError() instanceof UsageError);
    assertSame(BaseError(42).message, '42');
  },

  testConstruct: function () {
    assertSame(typeof(construct(Date, [])), 'object');
    function C(a, b) { this.sum = a + b; }
    assertSame(construct(C, [1, 2]).sum, 3);
    assertThrow(UsageError, construct);
    assertThrow(UsageError, construct, 42, []);
    assertThrow(TypeError, construct, function () {}, 42);
  },

  testRequestApp: function () {
    fs.list('').forEach(remove);
    fs.write('file1', 'wuzzup');
    fs.write('file2', 'yo ho ho');
    fs.write('hello', 'hello world!');
    assertSame(requestApp('another-app',
                          fs.read('hello'),
                          ['file1', 'file2'],
                          'yo!!!'),
               ('{"user":"' + _core.user + '",'+
                '"arg":"hello world!","data":"yo!!!",' +
                '"fileContents":["wuzzup","yo ho ho"],' +
                '"issuer":"test-app"}'));
    assert(!fs.exists('file1') && !fs.exists('file2'));
    fs.remove('hello');
    assertThrow(NoSuchAppError, requestApp, 'no-such-app', 'hi', [], null);
    assertThrow(TypeError, requestApp, 'another-app', '', 42, null);
    fs.write('file3', 'text');
    assertSame(requestApp('another-app', '', [], fs.read('file3')),
               ('{"user":"' + _core.user + '",' +
                '"arg":"",' +
                '"data":"text",' +
                '"fileContents":[],' +
                '"issuer":"test-app"}'));
    fs.remove('file3');
    assertThrow(NoSuchAppError, requestApp, 'invalid/app/name', '', [], null);
    assertThrow(UsageError, requestApp, 'test-app', '2+2', [], null);
    assertThrow(ProcessingFailedError,
                requestApp, 'throwing-app', '', [], null);
    assertThrow(TimedOutError, requestApp, 'blocking-app', '', [], null);
    assertThrow(PathError, requestApp, 'another-app', '', ['..'], null);
    assertThrow(NoSuchEntryError,
                requestApp, 'another-app', '', ['no-such-file'], null);
    fs.createDir('dir');
    assertThrow(EntryIsDirError, requestApp, 'another-app', '', ['dir'], null);
    assertThrow(TypeError, "requestApp('another-app', 'hi', 42, null)");
  },

  testRequestHost: function () {
    var response = requestHost('example.com', 80,
                               'GET / HTTP/1.0\r\n\r\n') + '';
    assertSame(response.substr(0, response.indexOf('\r')), 'HTTP/1.1 200 OK');
    assert(response.indexOf('2606') != -1);
    assertThrow(HostRequestError, requestHost, 'bad host name', 80, '');
  },

  testProxy: function () {
    assertSame(Proxy(), undefined);
    assertThrow(UsageError, "new Proxy()");
    assertThrow(TypeError, "new Proxy(42)");
    assertThrow(TypeError, "(new Proxy({})).x");
    assertEqual(
      keys(new Proxy({enumerate: function () { return 42; }})), []);
    assertEqual(
      keys(new Proxy({enumerate: function () { return {length: 1.5}; }})), []);
    assertEqual(
      keys(new Proxy({enumerate: function () { return {length: -15}; }})), []);
    var proxy = new Proxy(
      {
        object: {},

        get: function (name) {
          return (this.object.hasOwnProperty(name)
                  ? this.object[name]
                  : undefined);
        },

        set: function (name, value) {
          this.object[name] = value + '!';
        },

        query: function (name) {
          return this.object.hasOwnProperty(name);
        },

        del: function (name) {
          return delete this.object[name];
        },

        enumerate: function () {
          return keys(this.object);
        }
      });
    assertSame(proxy.x, undefined);
    assertEqual(keys(proxy), []);
    assert(!('x' in proxy));
    assert(!(0 in proxy));
    assert(!('hasOwnProperty' in proxy));
    proxy.x = 42;
    proxy.y = 'yo';
    proxy[0] = 'hi';
    assert('x' in proxy);
    assert(0 in proxy);
    assertEqual(items(proxy), [['0', 'hi!'], ['x', '42!'], ['y', 'yo!']]);
    assert(delete proxy.x &&
           delete proxy.z &&
           delete proxy[0] &&
           delete proxy[1]);
    assertEqual(items(proxy), [['y', 'yo!']]);

    function E() {}
    assertThrow(
      E, function () { (new Proxy({get get() { throw new E(); }})).x += 42; });
    function throwE() { throw new E(); }
    var throwProxy = new Proxy(
      {
        get: throwE,
        set: throwE,
        query: throwE,
        del: throwE,
        enumerate: throwE
      });
    assertThrow(E, function () { throwProxy.x += 42; });
    assertThrow(E, function () { throwProxy.x = 42; });
    assert(!('x' in throwProxy ||
             0 in throwProxy ||
             delete throwProxy.x ||
             delete throwProxy[0]));
    assertEqual(keys(throwProxy), []);
    assertEqual(
      keys(
        new Proxy(
          {
            enumerate: function () {
              return {get length() { throw Error(); }};
            }
          })),
      []);
    assertEqual(
      keys(
        new Proxy(
          {
            enumerate: function () {
              var object = {length: 2, 0: 0};
              object.__defineGetter__(1, function () { throw Error(); });
              return object;
            }
          })),
      []);
  }
};

////////////////////////////////////////////////////////////////////////////////
// DB test suite
////////////////////////////////////////////////////////////////////////////////

var dbTestSuite = {
  setUp: function () {
    db.drop(db.list());

    create('Dummy', {id: number});
    create('Empty', {});
    create(
      'User',
      {
        id: number._serial()._unique(),
        name: string._unique(),
        age: number,
        flooder: bool._default('yo!')
      });
    create(
      'Post',
      {
        id: number._serial()._unique(),
        title: string,
        text: string,
        author: number._integer()._foreign('User', 'id')
      },
      {unique: [['title', 'author']]});
    create(
      'Comment',
      {
        id: number._serial()._unique(),
        text: string,
        author: number._integer()._foreign('User', 'id'),
        post: number._integer()._foreign('Post', 'id')
      });

    db.insert('User', {name: 'anton', age: 22, flooder: 15});
    db.insert('User', {name: 'marina', age: 25, flooder: false});
    db.insert('User', {name: 'den', age: 23});

    db.insert('Post', {title: 'first', text: 'hello world', author: 0});
    db.insert('Post', {title: 'second', text: 'hi', author: 1});
    db.insert('Post', {title: 'third', text: 'yo!', author: 0});

    db.insert('Comment', {text: 42, author: 1, post: 0});
    db.insert('Comment', {text: 'rrr', author: 0, post: 0});
    db.insert('Comment', {text: 'ololo', author: 2, post: 2});

    create('Count', {i: number});
    for (var i = 0; i < 10; ++i)
      db.insert('Count', {i: i});

    assertEqual(db.list().sort(),
                ['Comment', 'Count', 'Dummy', 'Empty', 'Post', 'User']);
  },

  tearDown: function () {
    db.drop(db.list());
  },

  testCreate: function () {
    assertThrow(UsageError, "db.create('illegal', {})");
    assertThrow(UsageError, create, '', {});
    assertThrow(UsageError, create, '123bad', {});
    assertThrow(UsageError, create, 'illegal', {'_@': number});
    assertThrow(TypeError, create, 'illegal', 'str');
    assertThrow(TypeError, "db.create('illegal', {}, 'str', [], [])");
    assertThrow(TypeError, create, 'illegal', {field: 15});
    assertThrow(RelVarExistsError, create, 'User', {});

    assertThrow(UsageError,
                create, 'illegal', {x: number}, {unique: [[]]});
    assertThrow(UsageError,
                create, 'illegal',
                        {x: number, y: number},
                        {foreign: [['x', 'y'], 'User', ['id']]});
    assertThrow(TypeError,
                create, 'illegal',
                        {'id': number},
                        {foreign: [[['id'], 'Post', 'id']]});
    assertThrow(UsageError,
                create, 'illegal',
                        {x: number, y: number},
                        {foreign: [[['x', 'y'],
                                    'Post',
                                    ['id', 'author']]]});
    assertThrow(DBQuotaError,
                function () {
                  var name = '';
                  for (var i = 0; i < 61; ++i)
                    name += 'x';
                  create(name, {});
                });
    assertThrow(DBQuotaError,
                function () {
                  attrs = {};
                  for (var i = 0; i < 1000; ++i)
                    attrs['attr' + i] = number;
                  create('illegal', attrs);
                });

    create('legal', {x: bool._default(new Date())});
    db.insert('legal', {});
    assertSame(query('legal')[0].x, true);
    db.drop(['legal']);
    assertThrow(TypeError, create, 'illegal', {x: date._default('illegal')});

    assertThrow(UsageError,
                create, 'illegal', {}, {foreign: [['a', 'b']]});
    assertThrow(UsageError,
                create, 'illegal',
                        {a: number},
                        {unique: [['a', 'a']]});
    assertThrow(UsageError,
                create, 'illegal',
                        {x: number},
                        {foreign: [[[], 'User', []]]});
    assertThrow(UsageError,
                create, 'illegal',
                        {x: number},
                        {foreign: [[['x'], 'User', ['age']]]});
    assertThrow(UsageError,
                create, 'illegal',
                        {x: number},
                        {foreign: [[['x'], 'User', ['id', 'age']]]});
  },

  testDropRelVars: function () {
    create('NewRelVar', {x: number});
    db.drop(['NewRelVar']);
    assertSame(db.list().indexOf('NewRelVar'), -1);
    assertThrow(RelVarDependencyError, "db.drop(['User'])");
    assertThrow(RelVarDependencyError, "db.drop(['User', 'Post'])");
    assertThrow(UsageError, "db.drop(['Comment', 'Comment'])");
    create('rv1', {x: number._unique()});
    create('rv2', {x: number._foreign('rv1', 'x')});
    assertThrow(RelVarDependencyError, "db.drop(['rv1'])");
    db.drop(['rv1', 'rv2']);
    assertThrow(NoSuchRelVarError, "db.drop(['Comment', 'NoSuch'])");
  },

  testQuery: function () {
    assertThrow(UsageError, "db.query()");
    assertThrow(TypeError, query, 'User', {});
    assertThrow(TypeError, query, 'User', {length: 0.5});
    assertThrow(TypeError, query, 'User', {length: -1});
    function E() {}
    assertThrow(E, query, 'User', {get length() { throw new E(); }});
    var object = {length: 1};
    object.__defineGetter__(0, function () { throw new E(); });
    assertThrow(E, query, 'User', object);

    assertEqual(
      query('User[name, age, flooder] where +id == "0"').map(items),
      [[['name', 'anton'], ['age', 22], ['flooder', true]]]);
    assertThrow(NoSuchRelVarError, query, 'dfsa');
    assertEqual(
      query('Post.author->name where id == $', [0]).map(items),
      [[['name', 'anton']]]);
    assertThrow(FieldError, query, 'User.asdf');
    assertEqual(
      query('User[id, name] where id == $1 && name == $2',
            [0, 'anton']).map(items),
      [[['id', 0], ['name', 'anton']]]);
    assertSame(query('User where forsome (x in {}) true').length, 3);
    assertThrow(QueryError, query, '{i: 1} where i->name');
    assertEqual(
      field('title', ' Post where author->name == $', ['anton']).sort(),
      ['first', 'third']);
    assertEqual(field('age', 'User where name == \'anton\''), [22]);
    assertEqual(field('age', 'User where name == "den"'), [23]);
    assertThrow(QueryError, query, 'for (x in {f: 1}) x.f->k');
    assertThrow(QueryError, query, '{f: 1}', [], ['f->k']);
  },

  testInsert: function () {
    assertThrow(UsageError, "db.insert('User')");
    assertThrow(TypeError, "db.insert('User', 15)");
    assertThrow(FieldError, "db.insert('User', {'@': 'abc'})");
    assertThrow(ConstraintError,
               "db.insert('Comment', {id: 2, text: 'yo', author: 5, post: 0})");
    assertThrow(FieldError,
               "db.insert('User', {id: 2})");
    assertThrow(FieldError,
               "db.insert('Empty', {x: 5})");
    assertEqual(
      items(db.insert('User', {name: 'xxx', age: false})),
      [['id', 3], ['name', 'xxx'], ['age', 0], ['flooder', true]]);
    var tuple = db.insert('User', {id: 4, name: 'yyy', age: 'asdf'});
    assert(isNaN(tuple.age));
    assertThrow(ConstraintError,
               "db.insert('User', {id: 'asdf', name: 'zzz', age: 42})");
    assertThrow(ConstraintError,
               "db.insert('User', {name: 'zzz', age: 42})");
    db.del('User', 'id >= 3', []);
    assertEqual(items(db.insert('Empty', {})), []);
    assertThrow(ConstraintError, "db.insert('Empty', {})");
    db.del('Empty', 'true', []);
    create('Num', {n: number, i: number._integer()._default(3.14)});
    assertSame(db.insert('Num', {n: 0}).i, 3);
    assertSame(db.insert('Num', {n: 1.5, i: 1.5}).i, 2);
    assertSame(db.insert('Num', {n: Infinity}).n, Infinity);
    assertSame(db.insert('Num', {n: -Infinity}).n, -Infinity);
    assert(isNaN(db.insert('Num', {n: NaN}).n));
    assertThrow(ConstraintError, "db.insert('Num', {n: 0, i: Infinity})");
    assertThrow(ConstraintError, "db.insert('Num', {n: 0, i: -Infinity})");
    assertThrow(ConstraintError, "db.insert('Num', {n: 0, i: NaN})");
    db.drop(['Num']);
  },

  testGetHeader: function () {
    assertEqual(items(db.getHeader('User')).sort(),
                 [['age', 'number'],
                  ['flooder', 'bool'],
                  ['id', 'serial'],
                  ['name', 'string']]);
  },

  testBy: function () {
    create('ByTest', {x: number, y: number});
    db.insert('ByTest', {x: 0, y: 0});
    db.insert('ByTest', {x: 1, y: 14});
    db.insert('ByTest', {x: 2, y: 21});
    db.insert('ByTest', {x: 3, y: 0});
    db.insert('ByTest', {x: 4, y: 0});
    db.insert('ByTest', {x: 5, y: 5});
    db.insert('ByTest', {x: 8, y: 3});
    db.insert('ByTest', {x: 9, y: 32});
    assertEqual(
      query('ByTest where y != $',
            [5],
            ['x * $1 % $2', 'y % $3'],
            [2, 7, 10],
            3, 3).map(items),
      [[['x', 1], ['y', 14]], [['x', 2], ['y', 21]], [['x', 9], ['y', 32]]]);
    assertEqual(field('x', 'ByTest', [], ['x'], [], 5), [5, 8, 9]);
    assertEqual(field('x', 'ByTest', [], ['x'], [], 0, 2), [0, 1]);
    assertSame(query('ByTest', [], [], [], 3, 6).length, 5);
    db.drop(['ByTest']);
  },

  testStartLength: function () {
    assertEqual(field('i', 'Count', [], ['i'], [], 8), [8, 9]);
    assertEqual(field('i', 'Count', [], ['i'], [], 6, 2), [6, 7]);
    assertEqual(field('i', 'Count', [], ['i'], [], 10), []);
    assertEqual(field('i', 'Count where i != 5', [], ['i'], [], 1, 6),
                [1, 2, 3, 4, 6, 7]);
    assertThrow(TypeError, query, 'Count', [], ['i'], [], -1);
  },

  testCount: function () {
    assertEqual(db.count('User', []), 3);
    assertEqual(db.count('union({i: 1}, {i: 2}, {i: 3})', []), 3);
    assertSame(db.count('Post.author where id < 2 && author->flooder', []), 1);
  },

  testAll: function () {
    assertEqual(field('id', 'User').sort(), [0, 1, 2]);
    assertEqual(field('name', 'User where !(id % 2)', [], ['-name']),
                ['den', 'anton']);
    assertSame(query('User where id == $', [0])[0]['name'], 'anton');
    assertEqual(field('id', 'User where name != $', ['den'], ['-id']), [1, 0]);
  },

  testUpdate: function () {
    var initial = query('User');
    assertThrow(UsageError, "db.update('User', 'id == 0', [], {}, [])");
    assertThrow(UsageError, "db.update('User', 'id == 0', [], {})");
    assertThrow(TypeError, "db.update('User', 'id == 0', [], 1, [])");
    assertThrow(ConstraintError,
               "db.update('User', 'id == 0', [], {id: '$'}, ['asdf'])");
    assertEqual(db.update('User', 'id == 0', [], {name: '$'}, ['ANTON']), 1);
    assertEqual(field('name', 'User where id == 0'), ['ANTON']);
    assertSame(db.update('User',
                         'name != $',
                         ['marina'],
                         {age: 'age + $1', flooder: 'flooder || $2'},
                         [2, 'yo!']),
               2);
    for (var i = 0; i < 10; ++i)
      assertThrow(
        ConstraintError,
        "db.update('User', 'name == $', ['den'], {id: '4'}, [])");
    initial.forEach(
      function (tuple) {
        db.update('User', 'id == $', [tuple.id],
                  {name: '$1', age: '$2', flooder: '$3'},
                  [tuple.name, tuple.age, tuple.flooder]);
      });
    assertEqual(query('User').map(items), initial.map(items));
  },

  testDel: function () {
    assertThrow(ConstraintError, "db.del('User', 'true', [])");
    var name = "xx'y'zz'";
    db.insert('User', {id: 3, name: name, age: 15, flooder: true});
    db.insert('Post', {id: 3, title: "", text: "", author: 3});
    db.update('User', 'id == 3', [], {name: 'name + 1'}, []);
    assertEqual(field('name', 'User where id == 3'), [name + 1]);
    assertEqual(db.del('Post', 'author->name == $', [name + 1]), 1);
    assertSame(db.del('User', 'age == $', [15]), 1);
    assertEqual(field('id', 'User', [], ['id']), [0, 1, 2]);
  },

  testStress: function () {
    for (var i = 0; i < 10; ++i) {
      assertEqual(
        query(('User[id, age] where ' +
               'flooder && ' +
               '(forsome (Comment) ' +
               ' author == User.id && post->author->name == $)'),
              ['anton'],
              ['id']).map(items),
        [[['id', 0], ['age', 22]], [['id', 2], ['age', 23]]]);
      this.testUpdate();
      this.testDel();
    };
  },

  testPg: function () {
    create('pg_class', {x: number});
    db.insert('pg_class', {x: 0});
    assertEqual(query('pg_class').map(items), [[['x', 0]]]);
    db.drop(['pg_class']);
  },

  testCheck: function () {
    create('silly', {n: number._check('n != 42')});
    create('dummy', {b: bool, s: string}, {check: ['b || s == "hello"']});
    db.insert('silly', {n: 0});
    assertThrow(ConstraintError, "db.insert('silly', {n: 42})");
    db.insert('dummy', {b: true, s: 'hi'});
    db.insert('dummy', {b: false, s: 'hello'});
    assertThrow(ConstraintError,
               "db.insert('dummy', {b: false, s: 'oops'})");
    db.drop(['silly', 'dummy']);
  },

  testDate: function () {
    create('d1', {d: date}, {unique: [['d']]});
    var someDate = new Date('Wed, Mar 04 2009 16:12:09 GMT');
    var otherDate = new Date(2009, 0, 15, 13, 27, 11, 481);
    db.insert('d1', {d: someDate});
    assertEqual(field('d', 'd1'), [someDate]);
    create('d2', {d: date}, {foreign: [[['d'], 'd1', ['d']]]});
    assertThrow(ConstraintError,
               function () { db.insert('d2', {d: otherDate}); });
    db.insert('d1', {d: otherDate});
    assertEqual(field('d', 'd1', [], ['-d']), [someDate, otherDate]);
    db.insert('d2', {d: otherDate});
    db.insert('d1', {d: 3.14});
    db.insert('d1', {d: false});
    db.insert('d1', {d: 'Sat, 27 Feb 2010 16:14:20 GMT'});
    assertThrow(TypeError, "db.insert('d1', {d: new Date('invalid')})");
    assertThrow(TypeError, "db.insert('d1', {d: 'invalid'})");
    db.drop(['d1', 'd2']);
  },

  testDefault: function () {
    var now = new Date();
    create('def',
           {
             n: number._default(42),
             s: string._default('hello, world!'),
             b: bool._default(true),
             d: date._default(now)
           });
    assertEqual(items(db.getDefault('def')).sort(),
                [['b', true],
                 ['d', now],
                 ['n', 42],
                 ['s', 'hello, world!']]);
    db.insert('def', {});
    assertThrow(ConstraintError, "db.insert('def', {})");
    db.insert('def', {b: false});
    db.insert('def', {n: 0, s: 'hi'});
    assertEqual(query('def', [], ['b', 'n']).map(items),
                [[['n', 42], ['s', 'hello, world!'], ['b', false], ['d', now]],
                 [['n', 0], ['s', 'hi'], ['b', true], ['d', now]],
                 [['n', 42], ['s', 'hello, world!'], ['b', true], ['d', now]]]);
    db.drop(['def']);
  },

  testIntegerSerial: function () {
    assertThrow(UsageError, "number._serial()._default(42)");
    assertThrow(UsageError, "number._default(42)._serial()");
    assertThrow(UsageError, "number._serial()._integer()");
    assertThrow(UsageError, "number._serial()._serial()");
    assertThrow(UsageError, "number._integer()._integer()");
    assertEqual(db.getInteger('Comment').sort(), ['author', 'id', 'post']);
    create('rv',
           {
             x: number._serial(),
             y: number._serial(),
             z: number._integer()
           });
    assertEqual(db.getInteger('rv').sort(), ['x', 'y', 'z']);
    assertEqual(db.getSerial('rv').sort(), ['x', 'y']);
    db.drop(['rv']);
  },

  testUnique: function () {
    create('rv',
           {a: number, b: string, c: bool},
           {unique: [['a', 'b'], ['b', 'c'], ['c']]});
    assertEqual(db.getUnique('rv').sort(), [['a', 'b'], ['b', 'c'], ['c']]);
    assertEqual(db.getUnique('Dummy'), [['id']]);
    db.drop(['rv']);
  },

  testForeignKey: function () {
    create('rv',
           {
             title: string,
             author: number._integer(),
             id: number._serial(),
             ref: number._integer()
           },
           {
             foreign: [
               [['title', 'author'], 'Post', ['title', 'author']],
               [['ref'], 'rv', ['id']]],
             unique: [['id']]
           });
    assertEqual(db.getForeign('rv').sort(),
                [
                  [['ref'], 'rv', ['id']],
                  [['title', 'author'], 'Post', ['title', 'author']]
                ]);
    db.drop(['rv']);
  },

  testRelVarNumber: function () {
    assertThrow(DBQuotaError,
                function () {
                  for (var i = 0; i < 500; ++i)
                    create('rv' + i, {});
                });
    assertThrow(NoSuchRelVarError,
                function () {
                  for (var i = 0; i < 500; ++i)
                    db.drop(['rv' + i]);
                });
  },

  testQuota: function () {
    assert(_core.dbQuota > 0);
    create('rv', {i: number._integer()._unique(), s: string});
    var str = 'x';
    for (var i = 0; i < 20; ++i)
      str += str;
    assertThrow(ConstraintError,
                function () { db.insert('rv', {i: 0, s: str + 'x'}); });
    // Slow DB size quota test, uncomment to run
//     assertThrow(
//       DBQuotaError,
//       function () {
//         for (var i = 0; ; ++i)
//           db.insert('rv', {i: i, s: str});
//       });
    db.drop(['rv']);
  },

  testGetAppDescription: function () {
    assertEqual(items(db.getAppDescription('test-app')),
                [['admin', 'test user'],
                 ['developers', ['Odysseus', 'Achilles']],
                 ['summary', 'test app'],
                 ['description', 'test app...'],
                 ['labels', ['1', '2']]]);
    assertEqual(items(db.getAppDescription('another-app')),
                [['admin', 'Odysseus'],
                 ['developers', []],
                 ['summary', 'another app'],
                 ['description', 'another app...'],
                 ['labels', ['1']]]);
    assertThrow(NoSuchAppError, "db.getAppDescription('no-such-app')");
    assertThrow(UsageError, "db.getAppDescription()");
  },

  testGetAdminedApps: function () {
    assertEqual(db.getAdminedApps('test user').sort(),
                ['bad-app', 'blocking-app',
                 'lib', 'test-app', 'throwing-app']);
    assertEqual(db.getAdminedApps('Achilles'), []);
    assertThrow(NoSuchUserError, "db.getAdminedApps('no such user')");
  },

  testGetDevelopedApps: function () {
    assertEqual(db.getDevelopedApps('Odysseus').sort(), ['lib', 'test-app']);
    assertEqual(db.getDevelopedApps('test user'), []);
    assertThrow(NoSuchUserError, "db.getDevelopedApps('no such user')");
  },

  testGetAppsByLabel: function () {
    assertEqual(db.getAppsByLabel('1').sort(), ['another-app', 'test-app']);
    assertEqual(db.getAppsByLabel('2'), ['test-app']);
    assertEqual(db.getAppsByLabel('no such label'), []);
  },

  testBigIndexRow: function () {
    create('rv', {s: string});
    assertThrow(DBError, "db.insert('rv', {s: readCode('main.js')})");
    db.drop(['rv']);
  }
};

////////////////////////////////////////////////////////////////////////////////
// File test suite
////////////////////////////////////////////////////////////////////////////////

var fileTestSuite = {
  setUp: function ()
  {
    fs.list('').forEach(remove);
    fs.createDir('dir1');
    fs.createDir('dir2');
    fs.createDir('dir1/subdir');
    fs.write('file', 'some text');
    fs.write('dir1/subdir/hello', 'hello world!');
    fs.write('dir1/subdir/привет', 'привет!');
  },

  tearDown: function () {
    fs.list('').forEach(remove);
  },

  testRead: function () {
    assertEqual(fs.read('//dir1////subdir/hello'), 'hello world!');
    var data = fs.read('/dir1/subdir/привет');
    assertEqual(data, 'привет!');
    assertThrow(ConversionError, function () { data.toString('UTF-32'); });
    assertThrow(UsageError, function () { data.toString('NO-SUCH-ENC'); });
    assertThrow(NoSuchEntryError, "fs.read('does not exists')");
    assertThrow(EntryIsDirError, "fs.read('dir1')");
    assertThrow(PathError, "fs.read('//..//test-app/dir1/subdir/hello')");
    assertThrow(PathError, "fs.read('/dir1/../../file')");
    assertThrow(PathError, "fs.read('////')");
    assertThrow(
      PathError,
      function () {
        var path = '';
        for (var i = 0; i < 40; ++i)
          path += '/dir';
        fs.read(path);
      });
  },

  testExists: function () {
    assertSame(fs.exists(''), true);
    assertSame(fs.exists('dir1/subdir/hello'), true);
    assertSame(fs.exists('no/such'), false);
  },

  testIsDir: function () {
    assertSame(fs.isDir(''), true);
    assertSame(fs.isDir('dir2'), true);
    assertSame(fs.isDir('file'), false);
    assertSame(fs.isDir('no/such'), false);
  },

  testIsFile: function () {
    assertSame(fs.isFile(''), false);
    assertSame(fs.isFile('dir1/subdir/hello'), true);
    assertSame(fs.isFile('dir1/subdir'), false);
    assertSame(fs.isFile('no/such'), false);
  },

  testList: function () {
    assertEqual(fs.list('').sort(), ['dir1', 'dir2', 'file']);
    assertThrow(NoSuchEntryError, "fs.list('no such dir')");
  },

  testGetModDate: function () {
    fs.write('hello', '');
    assert(Math.abs(new Date() - fs.getModDate('hello')) < 2000);
    assertThrow(NoSuchEntryError, "fs.getModDate('no-such-file')");
    remove('hello');
  },

  testMakeDir: function () {
    fs.createDir('dir2/ddd');
    assertEqual(fs.list('dir2'), ['ddd']);
    assertEqual(fs.list('dir2/ddd'), []);
    remove('dir2/ddd');
    assertThrow(EntryExistsError, "fs.createDir('file')");
  },

  testWrite: function () {
    fs.write('wuzzup', 'yo wuzzup!');
    assertEqual(fs.read('wuzzup'), 'yo wuzzup!');
    remove('wuzzup');
    fs.write('hello', fs.read('dir1/subdir/hello'));
    assertEqual(fs.read('hello'), 'hello world!');
    remove('hello');
    assertThrow(EntryIsNotDirError, "fs.write('file/xxx', '')");
    assertThrow(PathError,
                function () {
                  var array = [];
                  for (var i = 0; i < 1000; ++i)
                    array.push('x');
                  fs.write(array.join(''), '');
                });
  },

  testRemove: function () {
    fs.write('new-file', 'data');
    remove('new-file');
    fs.createDir('dir2/new-dir');
    remove('dir2/new-dir');
    assertEqual(fs.list('').sort(), ['dir1', 'dir2', 'file']);
    assertEqual(fs.list('dir2'), []);
    assertThrow(DirIsNotEmptyError, "fs.remove('dir1')");
  },

  testRename: function () {
    fs.rename('dir1', 'dir2/dir3');
    assertEqual(fs.read('dir2/dir3/subdir/hello'), 'hello world!');
    fs.rename('dir2/dir3', 'dir1');
    assertThrow(NoSuchEntryError, "fs.rename('no such file', 'xxx')");
  },

  testQuota: function () {
    var array = [];
    for (var i = 0; i < _core.fsQuota / 2; ++i)
      array.push('x');
    var str = array.join('');
    fs.write('file1', str);
    assertThrow(FSQuotaError, function () { fs.write('file2', str); });
    remove('file1');
    assert(!fs.exists('file2'));
  }
};

////////////////////////////////////////////////////////////////////////////////
// Entry point
////////////////////////////////////////////////////////////////////////////////

main = function () {
  return runTestSuites([baseTestSuite, dbTestSuite, fileTestSuite]);
};


_core.main = function (expr) {
  return eval(expr);
};


'main.js value';
