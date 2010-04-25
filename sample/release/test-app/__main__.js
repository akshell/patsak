
// (c) 2009-2010 by Anton Korenyushkin

_core.include('MochiKit.js');

var include = _core.include;
var Script = _core.Script;
var db = _core.db;
var fs = _core.fs;

var number = db.number;
var string = db.string;
var bool = db.bool;
var date = db.date;

var READ_ONLY   = 1 << 0;
var DONT_ENUM   = 1 << 1;
var DONT_DELETE = 1 << 2;


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


function query(query, query_params, by, by_params, start, length) {
  return db.query(query,
                  query_params || [],
                  by || [],
                  by_params || [],
                  start || 0,
                  length);
}


function field(name, str, params, by, by_params, start, length) {
  return Array.prototype.map.call(
    query('for (x in ' + str + ') x.' + name,
          params, by, by_params, start, length),
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

////////////////////////////////////////////////////////////////////////////////
// Test tools
////////////////////////////////////////////////////////////////////////////////

var error_count;
var prefix = '### ';


function error(descr) {
  print(prefix + descr);
  ++error_count;
}


function check(expr, descr) {
  if (expr instanceof Function) {
    descr = descr || expr + '';
    expr = expr();
  } else if (typeof(expr) == 'string') {
    descr = descr || expr;
    expr = eval(expr);
  }
  if (!expr)
    error(descr + ' check failed');
}


function checkEqualTo(expr, value) {
  var expr_value;
  if (expr instanceof Function)
    expr_value = expr();
  else
    expr_value = eval(expr);
  if (compare(expr_value, value) != 0)
    error(expr + ' value ' + repr(expr_value) + ' != ' + repr(value));
}


function checkThrow(cls, expr) {
  try {
    if (expr instanceof Function)
      expr();
    else
      eval(expr);
  } catch (err) {
    if (err instanceof cls)
      return;
    error(expr + ' has thrown an unexpected error ' + err);
    return;
  }
  error(expr + ' was expected to throw');
}


function runTestSuite(test_suite) {
  var test_count = 0;
  if (test_suite.setUp)
    test_suite.setUp();
  for (var field in test_suite) {
    if (startsWith('test', field)) {
      test_suite[field]();
      ++test_count;
    }
  }
  if (test_suite.tearDown)
    test_suite.tearDown();
  return test_count;
}


function runTestSuites(test_suites) {
  error_count = 0;
  var test_count = 0;
  forEach(test_suites,
          function (ts) { test_count += runTestSuite(ts); });
  print(prefix + (error_count ? error_count : 'No') +
        ' errors detected in ' +
        test_count + ' test cases in ' +
        test_suites.length + ' test suites');
  return test_count;
}

////////////////////////////////////////////////////////////////////////////////
// Base test suite
////////////////////////////////////////////////////////////////////////////////

var base_test_suite = {};


var path = include.path;

base_test_suite.testInclude = function ()  {
  check("path == '__main__.js'");
  check("include.path === undefined");
  check("include('hello.js') == 'hello'");
  check("include('subdir/another-hello.js') == 'hello'");
  checkThrow(UsageError, "include()");
  checkThrow(NoSuchEntryError, "include('no-such-file.js')");
  checkThrow(SyntaxError, "include('bug.js')");
  checkThrow(SyntaxError, "include('bug-includer.js')");
  checkThrow(PathError, "include('../out-of-base-dir.js')");
  checkThrow(UsageError, "include('self-includer.js')");
  checkThrow(UsageError, "include('cycle-includer1.js')");
  checkThrow(NoSuchAppError, "include('no_such_lib', 'xxx.js')");
  checkEqualTo("include('lib', '0.1/42.js')", 42);
  include('//subdir///once.js');
  include('subdir/../subdir//./once.js');
  checkThrow(PathError, "include('')");
  checkThrow(PathError, "include('..')");
  checkThrow(PathError, "include('subdir/..')");
  check("include('__main__.js') == '__main__.js value'");
};


base_test_suite.testUse = function () {
  check("_core.use('lib', '0.1') == 42");
};


base_test_suite.testType = function () {
  check("number.name == 'number'");
  check("string.name == 'string'");
  check("bool.name == 'bool'");
  check("date.name == 'date'");
};


base_test_suite.testConstructors = function () {
  check("this instanceof _core.Global");
  check("_core instanceof _core.Core");
  check("db instanceof _core.DB");
  check("keys(_core).indexOf('Core') != -1");
};


base_test_suite.testSet = function () {
  var obj = {};
  checkThrow(UsageError, "set()");
  checkThrow(TypeError, "set(1, 'f', 0, 42)");
  checkEqualTo(
    function () {
      return set(obj, 'read_only', READ_ONLY, 1);
    },
    obj);
  set(obj, 'dont_enum', DONT_ENUM, 2);
  set(obj, 'dont_delete', DONT_DELETE, 3);
  checkThrow(TypeError,
             function () { set(obj, 'field', {}, 42); });
  checkThrow(UsageError,
             function () { set(obj, 'field', 8, 42); });
  check(function () {
          obj.read_only = 5;
          return obj.read_only == 1;
        });
  checkEqualTo(function () { return keys(obj); },
               ['read_only', 'dont_delete']);
  check(function () {
          delete obj.dont_delete;
          return obj.dont_delete == 3;
        });
};


base_test_suite.testAppName = function () {
  check("_core.app.name == 'test-app'");
};


base_test_suite.testReadCode = function () {
  check("readCode('subdir/hi.txt') == 'russian привет\\n'");
  check("readCode('bad-app', '__main__.js') == 'wuzzup!!!!!!!!\\n'");
  checkThrow(NoSuchAppError,
             "readCode('illegal/name', 'main.js')");
  checkThrow(PathError, "readCode('test-app', '')");
  checkThrow(PathError,
             "readCode('test-app', 'subdir/../../another-app/__main__.js')");
  checkThrow(UsageError, "readCode()");
};


base_test_suite.testGetCodeModDate = function () {
  check("getCodeModDate('__main__.js') > new Date('01.01.2010')");
  check("getCodeModDate('lib', '0.1/42.js') < new Date()");
  checkThrow(NoSuchEntryError, "getCodeModDate('no-such-file')");
  checkThrow(NoSuchEntryError, "getCodeModDate('lib', 'no-such-file')");
  checkThrow(NoSuchAppError, "getCodeModDate('no-such-app', 'file')");
};


base_test_suite.testScript = function () {
  check("(new Script('2+2'))._run() === 4");
  check("Script('2+2', 'name')._run() === 4");
  checkThrow(UsageError, "new Script()");
  checkThrow(SyntaxError, "new Script('(')");
  checkThrow(ReferenceError, "new Script('undeclarated')._run()");
  checkThrow(SyntaxError,
             "new Script('new Script(\"(\")', 'just string')._run()");
  try {
    new Script('undeclarated', 'some name', 10, 20)._run();
    check(false);
  } catch (error) {
    check(error instanceof ReferenceError);
    check(error.stack.indexOf('some name:11:21\n') != -1);
  }
  try {
    new Script('undeclarated', 'some name', 10)._run();
    check(false);
  } catch (error) {
    check(error.stack.indexOf('some name:11:1\n') != -1);
  }
  try {
    new Script('undeclarated', 'some name', {}, 20)._run();
    check(false);
  } catch (error) {
    check(error.stack.indexOf('some name:1:21\n') != -1);
  }
};


base_test_suite.testHash = function () {
  checkThrow(UsageError, "hash()");
  check("hash(undefined) == 0");
  check("hash(null) == 0");
  check("hash(42) == 0");
  check("hash('foo') == 0");
  check("hash('') == 0");
  check("hash({}) > 0");
  check("hash(function () {}) > 0");
};


base_test_suite.testErrors = function () {
  check("new BaseError() instanceof Error");
  check("new CoreError() instanceof BaseError");
  check("CoreError.__name__ == 'CoreError'");
  check("CoreError.prototype.name == 'CoreError'");
  check("new DBError() instanceof CoreError");
  check("new FieldError() instanceof DBError");
  check("UsageError() instanceof UsageError");
  check("BaseError(42).message === '42'");
};


base_test_suite.testConstruct = function () {
  check("typeof(construct(Date, [])) == 'object'");
  checkEqualTo(function () {
                 function C(a, b) {
                   this.sum = a + b;
                 }
                 return construct(C, [1, 2]).sum;
               },
               3);
  checkThrow(UsageError, "construct()");
  checkThrow(UsageError, "construct(42, [])");
  checkThrow(TypeError, "construct(function () {}, 42)");
};


base_test_suite.testRequestApp = function () {
  forEach(fs.list(''), remove);
  fs.write('file1', 'wuzzup');
  fs.write('file2', 'yo ho ho');
  fs.write('hello', 'hello world!');
  checkEqualTo(function () {
                 return requestApp('another-app',
                                   fs.read('hello'),
                                   ['file1', 'file2'],
                                   'yo!!!');
               },
               '{"user":"' + _core.user + '",'+
               '"arg":"hello world!","data":"yo!!!",' +
               '"file_contents":["wuzzup","yo ho ho"],' +
               '"issuer":"test-app"}');
  check("!fs.exists('file1') && !fs.exists('file2')");
  fs.remove('hello');
  checkThrow(NoSuchAppError,
             "requestApp('no-such-app', 'hi', [], null)");
  checkThrow(TypeError, "requestApp('another-app', '', 42, null)");
  checkEqualTo(
    function () {
      fs.write('file3', 'text');
      var result = requestApp('another-app', '', [], fs.read('file3'));
      fs.remove('file3');
      return result;
    },
    '{"user":"' + _core.user + '",' +
      '"arg":"",' +
      '"data":"text",' +
      '"file_contents":[],' +
      '"issuer":"test-app"}');
  checkThrow(NoSuchAppError,
             "requestApp('invalid/app/name', '', [], null)");
  checkThrow(UsageError, "requestApp('test-app', '2+2', [], null)");
  checkThrow(ProcessingFailedError,
             "requestApp('throwing-app', '', [], null)");
  checkThrow(TimedOutError, "requestApp('blocking-app', '', [], null)");
  checkThrow(PathError, "requestApp('another-app', '', ['..'], null)");
  checkThrow(NoSuchEntryError,
             "requestApp('another-app', '', ['no-such-file'], null)");
  fs.createDir('dir');
  checkThrow(EntryIsDirError,
             "requestApp('another-app', '', ['dir'], null)");
  checkThrow(TypeError, "requestApp('another-app', 'hi', 42, null)");
};


base_test_suite.testRequestHost = function () {
  var response = requestHost('example.com', 80, 'GET / HTTP/1.0\r\n\r\n') + '';
  check(response.substr(0, response.indexOf('\r')) == 'HTTP/1.1 200 OK');
  check(response.indexOf('2606') != -1);
  checkThrow(HostRequestError, "requestHost('bad host name', 80, '')");
};

////////////////////////////////////////////////////////////////////////////////
// DB test suite
////////////////////////////////////////////////////////////////////////////////

var db_test_suite = {};


function mapItems(iterable) {
  return map(items, iterable);
}


db_test_suite.setUp = function () {
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

  checkEqualTo("db.list().sort()",
               ["Comment", "Count", "Dummy", "Empty", "Post", "User"]);
};


db_test_suite.tearDown = function () {
  db.drop(db.list());
};


db_test_suite.testCreate = function () {
  checkThrow(UsageError, "db.create('illegal', {})");
  checkThrow(UsageError, "create('', {})");
  checkThrow(UsageError, "create('123bad', {})");
  checkThrow(UsageError, "create('illegal', {'_@': number})");
  checkThrow(TypeError, "create('illegal', 'str')");
  checkThrow(TypeError, "db.create('illegal', {}, 'str', [], [])");
  checkThrow(TypeError, "create('illegal', {field: 15})");
  checkThrow(RelVarExistsError, "create('User', {})");

  checkThrow(UsageError,
             function () {
               create('illegal', {x: number}, {unique: [[]]});
             });

  checkThrow(UsageError,
             function () {
               create('illegal',
                      {x: number, y: number},
                      {foreign: [['x', 'y'], 'User', ['id']]});
             });

  checkThrow(TypeError,
             function () {
               create('illegal',
                      {'id': number},
                      {foreign: [[['id'], 'Post', 'id']]});
             });
  checkThrow(UsageError,
             function () {
               create('illegal',
                      {x: number, y: number},
                      {foreign: [[['x', 'y'],
                                  'Post',
                                  ['id', 'author']]]});
             });
  checkThrow(DBQuotaError,
             function () {
               var name = '';
               for (var i = 0; i < 61; ++i)
                 name += 'x';
               create(name, {});
             });
  checkThrow(DBQuotaError,
             function () {
               attrs = {};
               for (var i = 0; i < 1000; ++i)
                 attrs['attr' + i] = number;
               create('illegal', attrs);
             });
  create('legal', {x: bool._default(new Date())});
  db.insert('legal', {});
  check("query('legal')[0].x === true");
  db.drop(['legal']);
  checkThrow(TypeError, "create('illegal', {x: date._default('illegal')})");

  checkThrow(UsageError,
             "create('illegal', {}, {foreign: [['a', 'b']]})");
  checkThrow(UsageError,
             function () {
               create('illegal',
                      {a: number},
                      {unique: [['a', 'a']]});
             });
  checkThrow(UsageError,
             function () {
               create('illegal',
                      {x: number},
                      {foreign: [[[], 'User', []]]});
             });
  checkThrow(UsageError,
             function () {
               create('illegal',
                      {x: number},
                      {foreign: [[['x'], 'User', ['age']]]});
             });
  checkThrow(UsageError,
             function () {
               create('illegal',
                      {x: number},
                      {foreign: [[['x'], 'User', ['id', 'age']]]});
             });
};


db_test_suite.testDropRelVars = function () {
  create('NewRelVar', {x: number});
  db.drop(['NewRelVar']);
  check("!('NewRelVar' in db.list())");
  checkThrow(RelVarDependencyError, "db.drop(['User'])");
  checkThrow(RelVarDependencyError, "db.drop(['User', 'Post'])");
  checkThrow(UsageError, "db.drop(['Comment', 'Comment'])");

  create('rv1', {x: number._unique()});
  create('rv2', {x: number._foreign('rv1', 'x')});
  checkThrow(RelVarDependencyError, "db.drop(['rv1'])");
  db.drop(['rv1', 'rv2']);
  checkThrow(NoSuchRelVarError, "db.drop(['Comment', 'no_such'])");
};


db_test_suite.testQuery = function () {
  checkEqualTo("mapItems(query('User[name, age, flooder] where +id == \"0\"'))",
               [[["name", "anton"], ["age", 22], ["flooder", true]]]);
  checkThrow(UsageError, "db.query()");
  checkThrow(TypeError, "query('User', {})");
  checkThrow(TypeError, "query('User', {length: 0.5})");
  checkThrow(TypeError, "query('User', {length: -1})");
  checkThrow(NoSuchRelVarError, "query('dfsa')");
  checkEqualTo(function () {
                 return mapItems(
                   query('Post.author->name where id == $', [0]));
               },
               [[['name', 'anton']]]);
  checkThrow(FieldError, "query('User.asdf')");
  checkEqualTo(function () {
                 return mapItems(
                   query('User[id, name] where id == $1 && name == $2',
                         [0, 'anton']));
               },
               [[['id', 0], ['name', 'anton']]]);
  checkEqualTo("query('User where forsome (x in {}) true').length", 3);
  checkThrow(QueryError, "query('{i: 1} where i->name')");
  checkEqualTo(
    "field('title', ' Post where author->name == $', ['anton']).sort()",
    ['first', 'third']);
  checkEqualTo("field('age', 'User where name == \\'anton\\'')", [22]);
  checkEqualTo("field('age', 'User where name == \"den\"')", [23]);
  checkThrow(QueryError, "query('for (x in {f: 1}) x.f->k')");
  checkThrow(QueryError, "query('{f: 1}', [], ['f->k'])");
};


db_test_suite.testInsert = function () {
  checkThrow(UsageError, "db.insert('User')");
  checkThrow(TypeError, "db.insert('User', 15)");
  checkThrow(FieldError, "db.insert('User', {'@': 'abc'})");
  checkThrow(ConstraintError,
             "db.insert('Comment', {id: 2, text: 'yo', author: 5, post: 0})");
  checkThrow(FieldError,
             "db.insert('User', {id: 2})");
  checkThrow(FieldError,
             "db.insert('Empty', {x: 5})");
  checkEqualTo(
    "items(db.insert('User', {name: 'xxx', age: false}))",
    [['id', 3], ['name', 'xxx'], ['age', 0], ['flooder', true]]);
  var tuple = db.insert('User', {id: 4, name: 'yyy', age: 'asdf'});
  check(isNaN(tuple.age));
  checkThrow(ConstraintError,
             "db.insert('User', {id: 'asdf', name: 'zzz', age: 42})");
  checkThrow(ConstraintError,
             "db.insert('User', {name: 'zzz', age: 42})");
  db.del('User', 'id >= 3', []);
  checkEqualTo("items(db.insert('Empty', {}))", []);
  checkThrow(ConstraintError, "db.insert('Empty', {})");
  db.del('Empty', 'true', []);
  create('Num', {n: number, i: number._integer()._default(3.14)});
  checkEqualTo("db.insert('Num', {n: 0}).i", 3);
  checkEqualTo("db.insert('Num', {n: 1.5, i: 1.5}).i", 2);
  checkEqualTo("db.insert('Num', {n: Infinity}).n", Infinity);
  checkEqualTo("db.insert('Num', {n: -Infinity}).n", -Infinity);
  check("isNaN(db.insert('Num', {n: NaN}).n)");
  checkThrow(ConstraintError, "db.insert('Num', {n: 0, i: Infinity})");
  checkThrow(ConstraintError, "db.insert('Num', {n: 0, i: -Infinity})");
  checkThrow(ConstraintError, "db.insert('Num', {n: 0, i: NaN})");
  db.drop(['Num']);
};


db_test_suite.testGetHeader = function () {
  checkEqualTo("items(db.getHeader('User')).sort()",
               [['age', 'number'],
                ['flooder', 'bool'],
                ['id', 'serial'],
                ['name', 'string']]);
};


db_test_suite.testBy = function () {
  create('ByTest', {x: number, y: number});
  db.insert('ByTest', {x: 0, y: 0});
  db.insert('ByTest', {x: 1, y: 14});
  db.insert('ByTest', {x: 2, y: 21});
  db.insert('ByTest', {x: 3, y: 0});
  db.insert('ByTest', {x: 4, y: 0});
  db.insert('ByTest', {x: 5, y: 5});
  db.insert('ByTest', {x: 8, y: 3});
  db.insert('ByTest', {x: 9, y: 32});
  checkEqualTo(
    function () {
      return mapItems(
        query('ByTest where y != $',
              [5],
              ['x * $1 % $2', 'y % $3'],
              [2, 7, 10],
              3, 3));
    },
    [[["x", 1], ["y", 14]], [["x", 2], ["y", 21]], [["x", 9], ["y", 32]]]);
  checkEqualTo("field('x', 'ByTest', [], ['x'], [], 5)", [5, 8, 9]);
  checkEqualTo("field('x', 'ByTest', [], ['x'], [], 0, 2)", [0, 1]);
  checkEqualTo("query('ByTest', [], [], [], 3, 6).length", 5);
  db.drop(['ByTest']);
};


db_test_suite.testStartLength = function () {
  checkEqualTo("field('i', 'Count', [], ['i'], [], 8)", [8, 9]);
  checkEqualTo("field('i', 'Count', [], ['i'], [], 6, 2)", [6, 7]);
  checkEqualTo("field('i', 'Count', [], ['i'], [], 10)", []);
  checkEqualTo("field('i', 'Count where i != 5', [], ['i'], [], 1, 6)",
               [1, 2, 3, 4, 6, 7]);
  checkThrow(TypeError, "query('Count', [], ['i'], [], -1)");
};


db_test_suite.testCount = function () {
  checkEqualTo("db.count('User', [])", 3);
  checkEqualTo("db.count('union({i: 1}, {i: 2}, {i: 3})', [])", 3);
  checkEqualTo(
    "db.count('Post.author where id < 2 && author->flooder', [])",
    1);
};


db_test_suite.testAll = function () {
  check("field('id', 'User').sort()", [0, 1, 2]);
  check("field('name', 'User where !(id % 2)', [], ['-name'])",
        ['den', 'anton']);
  check("query('User where id == $', [0])[0]['name']", 'anton');
  check("field('id', 'User where name != $', ['den'], ['-id'])", [1, 0]);
};


db_test_suite.testUpdate = function () {
  var initial = query('User');
  checkThrow(UsageError, "db.update('User', 'id == 0', [], {}, [])");
  checkThrow(UsageError, "db.update('User', 'id == 0', [], {})");
  checkThrow(TypeError, "db.update('User', 'id == 0', [], 1, [])");
  checkThrow(ConstraintError,
             "db.update('User', 'id == 0', [], {id: '$'}, ['asdf'])");
  checkEqualTo("db.update('User', 'id == 0', [], {name: '$'}, ['ANTON'])",
               1);
  checkEqualTo("field('name', 'User where id == 0')", ['ANTON']);
  var rows_number = db.update('User',
                              'name != $',
                              ['marina'],
                              {age: 'age + $1', flooder: 'flooder || $2'},
                              [2, 'yo!']);
  check(rows_number == 2);
  for (var i = 0; i < 10; ++i)
    checkThrow(
      ConstraintError,
      "db.update('User', 'name == $', ['den'], {id: '4'}, [])");
  forEach(initial, function (tuple) {
            db.update('User', 'id == $', [tuple.id],
                      {name: '$1', age: '$2', flooder: '$3'},
                      [tuple.name, tuple.age, tuple.flooder]);
          });
  checkEqualTo("mapItems(query('User'))", mapItems(initial));
};


db_test_suite.testDel = function () {
  checkThrow(ConstraintError, "db.del('User', 'true', [])");
  var tricky_name = 'xx\'y\'zz\'';
  db.insert('User', {id: 3, name: tricky_name, age: 15, flooder: true});
  db.insert('Post', {id: 3, title: "", text: "", author: 3});
  db.update('User', 'id == 3', [], {name: 'name + 1'}, []);
  checkEqualTo("field('name', 'User where id == 3')", [tricky_name + 1]);
  checkEqualTo("db.del('Post', 'author->name == $', ['xx\\'y\\'zz\\'1'])", 1);
  checkEqualTo("db.del('User', 'age == $', [15])", 1);
  checkEqualTo("field('id', 'User', [], ['id'])", [0, 1, 2]);
};


db_test_suite.testStress = function () {
  for (var i = 0; i < 10; ++i) {
    checkEqualTo(
      function () {
        return mapItems(
          query(('User[id, age] where ' +
                 'flooder && ' +
                 '(forsome (Comment) ' +
                 ' author == User.id && post->author->name == $)'),
                ['anton'],
                ['id']));
      },
      [[["id", 0], ["age", 22]], [["id", 2], ["age", 23]]]);
    this.testUpdate();
    this.testDel();
  };
};


db_test_suite.testPg = function () {
  create('pg_class', {x: number});
  db.insert('pg_class', {x: 0});
  checkEqualTo(function () { return mapItems(query('pg_class')); },
               [[['x', 0]]]);
  db.drop(['pg_class']);
};


db_test_suite.testCheck = function () {
  create('silly', {n: number._check('n != 42')});
  create('dummy', {b: bool, s: string}, {check: ['b || s == "hello"']});
  db.insert('silly', {n: 0});
  checkThrow(ConstraintError, "db.insert('silly', {n: 42})");
  db.insert('dummy', {b: true, s: 'hi'});
  db.insert('dummy', {b: false, s: 'hello'});
  checkThrow(ConstraintError,
             "db.insert('dummy', {b: false, s: 'oops'})");
  db.drop(['silly', 'dummy']);
};


db_test_suite.testDate = function () {
  create('d1', {d: date}, {unique: [['d']]});
  var some_date = new Date('Wed, Mar 04 2009 16:12:09 GMT');
  var other_date = new Date(2009, 0, 15, 13, 27, 11, 481);
  db.insert('d1', {d: some_date});
  checkEqualTo("field('d', 'd1')", [some_date]);
  create('d2', {d: date}, {foreign: [[['d'], 'd1', ['d']]]});
  checkThrow(ConstraintError,
             function () { db.insert('d2', {d: other_date}); });
  db.insert('d1', {d: other_date});
  checkEqualTo("field('d', 'd1', [], ['-d'])", [some_date, other_date]);
  db.insert('d2', {d: other_date});
  db.insert('d1', {d: 3.14});
  db.insert('d1', {d: false});
  db.insert('d1', {d: 'Sat, 27 Feb 2010 16:14:20 GMT'});
  checkThrow(TypeError, "db.insert('d1', {d: new Date('invalid')})");
  checkThrow(TypeError, "db.insert('d1', {d: 'invalid'})");
  db.drop(['d1', 'd2']);
};


db_test_suite.testDefault = function () {
  var now = new Date();
  create('def',
         {
           n: number._default(42),
           s: string._default('hello, world!'),
           b: bool._default(true),
           d: date._default(now)
         });
  checkEqualTo("items(db.getDefault('def')).sort()",
               [['b', true],
                ['d', now],
                ['n', 42],
                ['s', 'hello, world!']]);
  db.insert('def', {});
  checkThrow(ConstraintError, "db.insert('def', {})");
  db.insert('def', {b: false});
  db.insert('def', {n: 0, s: 'hi'});
  checkEqualTo(function () { return mapItems(query('def', [], ['b', 'n'])); },
               [[['n', 42], ['s', 'hello, world!'], ['b', false], ['d', now]],
                [['n', 0], ['s', 'hi'], ['b', true], ['d', now]],
                [['n', 42], ['s', 'hello, world!'], ['b', true], ['d', now]]]);
  db.drop(['def']);
};


db_test_suite.testIntegerSerial = function () {
  checkThrow(UsageError, "number._serial()._default(42)");
  checkThrow(UsageError, "number._default(42)._serial()");
  checkThrow(UsageError, "number._serial()._integer()");
  checkThrow(UsageError, "number._serial()._serial()");
  checkThrow(UsageError, "number._integer()._integer()");
  checkEqualTo("db.getInteger('Comment').sort()", ['author', 'id', 'post']);
  create('rv',
         {
           x: number._serial(),
           y: number._serial(),
           z: number._integer()
         });
  checkEqualTo("db.getInteger('rv').sort()", ['x', 'y', 'z']);
  checkEqualTo("db.getSerial('rv').sort()", ['x', 'y']);
  db.drop(['rv']);
};


db_test_suite.testUnique = function () {
  create('rv',
         {a: number, b: string, c: bool},
         {unique: [['a', 'b'], ['b', 'c'], ['c']]});
  checkEqualTo("db.getUnique('rv').sort()",
               [['a', 'b'], ['b', 'c'], ['c']]);
  checkEqualTo("db.getUnique('Dummy')", [['id']]);
  db.drop(['rv']);
};


db_test_suite.testForeignKey = function () {
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
  checkEqualTo("db.getForeign('rv').sort()",
               [
                 [["ref"], "rv", ["id"]],
                 [["title", "author"], "Post", ["title", "author"]]
               ]);
  db.drop(['rv']);
};


db_test_suite.testRelVarNumber = function () {
  checkThrow(DBQuotaError,
             function () {
               for (var i = 0; i < 500; ++i)
                 create('rv' + i, {});
             });
  checkThrow(NoSuchRelVarError,
             function () {
               for (var i = 0; i < 500; ++i)
                 db.drop(['rv' + i]);
             });
};


db_test_suite.testQuota = function () {
  check("_core.dbQuota > 0");
  create('rv', {i: number._integer()._unique(), s: string});
  var str = 'x';
  for (var i = 0; i < 20; ++i)
    str += str;
  checkThrow(ConstraintError,
             function () { db.insert('rv', {i: 0, s: str + 'x'}); });
  // Slow DB size quota test, uncomment to run
  //   checkThrow(
  //     DBQuotaError,
  //     function () {
  //       for (var i = 0; ; ++i)
  //         db.insert('rv', {i: i, s: str});
  //     });
  db.drop(['rv']);
};


db_test_suite.testGetAppDescription = function () {
  checkEqualTo(items(db.getAppDescription('test-app')),
               [['admin', 'test user'],
                ['developers', ['Odysseus', 'Achilles']],
                ['summary', 'test app'],
                ['description', 'test app...'],
                ['labels', ['1', '2']]]);
  checkEqualTo(items(db.getAppDescription('another-app')),
               [["admin", "Odysseus"],
                ["developers", []],
                ["summary", "another app"],
                ["description", "another app..."],
                ["labels", ["1"]]]);
  checkThrow(NoSuchAppError, "db.getAppDescription('no-such-app')");
  checkThrow(UsageError, "db.getAppDescription()");
};


db_test_suite.testGetAdminedApps = function () {
  checkEqualTo(db.getAdminedApps('test user').sort(),
               ['bad-app', 'blocking-app',
                'lib', 'test-app', 'throwing-app']);
  checkEqualTo(db.getAdminedApps('Achilles'), []);
  checkThrow(NoSuchUserError,
             function () { db.getAdminedApps('no_such_user'); });

};


db_test_suite.testGetDevelopedApps = function () {
  checkEqualTo(db.getDevelopedApps('Odysseus').sort(), ['lib', 'test-app']);
  checkEqualTo(db.getDevelopedApps('test user'), []);
  checkThrow(NoSuchUserError,
             function () { db.getDevelopedApps('no_such_user'); });
};


db_test_suite.testGetAppsByLabel = function () {
  checkEqualTo(db.getAppsByLabel('1').sort(), ['another-app', 'test-app']);
  checkEqualTo(db.getAppsByLabel('2'), ['test-app']);
  checkEqualTo(db.getAppsByLabel('no_such_label'), []);
};


db_test_suite.testBigIndexRow = function () {
  create('rv', {s: string});
  checkThrow(DBError, "db.insert('rv', {s: readCode('__main__.js')})");
  db.drop(['rv']);
};

////////////////////////////////////////////////////////////////////////////////
// File test suite
////////////////////////////////////////////////////////////////////////////////

var file_test_suite = {};


file_test_suite.setUp = function ()
{
  forEach(fs.list(''), remove);
  fs.createDir('dir1');
  fs.createDir('dir2');
  fs.createDir('dir1/subdir');
  fs.write('file', 'some text');
  fs.write('dir1/subdir/hello', 'hello world!');
  fs.write('dir1/subdir/привет', 'привет!');
};


file_test_suite.tearDown = function () {
  forEach(fs.list(''), remove);
};


file_test_suite.testRead = function () {
  checkEqualTo("fs.read('//dir1////subdir/hello')",
               'hello world!');
  var data = fs.read('/dir1/subdir/привет');
  checkEqualTo(data, 'привет!');
  checkThrow(ConversionError, function () { data.toString('UTF-32'); });
  checkThrow(UsageError, function () { data.toString('NO_SUCH_ENC'); });
  checkThrow(NoSuchEntryError, "fs.read('does_not_exists')");
  checkThrow(EntryIsDirError, "fs.read('dir1')");
  checkThrow(PathError, "fs.read('//..//test-app/dir1/subdir/hello')");
  checkThrow(PathError, "fs.read('/dir1/../../file')");
  checkThrow(PathError, "fs.read('////')");
  checkThrow(PathError,
             function () {
               var path = '';
               for (var i = 0; i < 40; ++i)
                 path += '/dir';
               fs.read(path);
             });
};


file_test_suite.testExists = function () {
  checkEqualTo("fs.exists('')", true);
  checkEqualTo("fs.exists('dir1/subdir/hello')", true);
  checkEqualTo("fs.exists('no/such')", false);
};


file_test_suite.testIsDir = function () {
  checkEqualTo("fs.isDir('')", true);
  checkEqualTo("fs.isDir('dir2')", true);
  checkEqualTo("fs.isDir('file')", false);
  checkEqualTo("fs.isDir('no/such')", false);
};


file_test_suite.testIsFile = function () {
  checkEqualTo("fs.isFile('')", false);
  checkEqualTo("fs.isFile('dir1/subdir/hello')", true);
  checkEqualTo("fs.isFile('dir1/subdir')", false);
  checkEqualTo("fs.isFile('no/such')", false);
};


file_test_suite.testList = function () {
  checkEqualTo("fs.list('').sort()", ['dir1', 'dir2', 'file']);
  checkThrow(NoSuchEntryError, "fs.list('no_such_dir')");
};


file_test_suite.testGetModDate = function () {
  fs.write('hello', '');
  check("Math.abs(new Date() - fs.getModDate('hello')) < 2000");
  checkThrow(NoSuchEntryError, "fs.getModDate('no-such-file')");
  remove('hello');
};


file_test_suite.testMakeDir = function () {
  fs.createDir('dir2/ddd');
  checkEqualTo("fs.list('dir2')", ['ddd']);
  checkEqualTo("fs.list('dir2/ddd')", []);
  remove('dir2/ddd');
  checkThrow(EntryExistsError, "fs.createDir('file')");
};


file_test_suite.testWrite = function () {
  fs.write('wuzzup', 'yo wuzzup!');
  checkEqualTo("fs.read('wuzzup')", 'yo wuzzup!');
  remove('wuzzup');
  fs.write('hello', fs.read('dir1/subdir/hello'));
  checkEqualTo("fs.read('hello')", 'hello world!');
  remove('hello');
  checkThrow(EntryIsNotDirError, "fs.write('file/xxx', '')");
  checkThrow(PathError,
             function () {
               var array = [];
               for (var i = 0; i < 1000; ++i)
                 array.push('x');
               fs.write(array.join(''), '');
             });
};


file_test_suite.testRemove = function () {
  fs.write('new-file', 'data');
  remove('new-file');
  fs.createDir('dir2/new-dir');
  remove('dir2/new-dir');
  checkEqualTo("fs.list('').sort()", ['dir1', 'dir2', 'file']);
  checkEqualTo("fs.list('dir2')", []);
  checkThrow(DirIsNotEmptyError, "fs.remove('dir1')");
};


file_test_suite.testRename = function () {
  fs.rename('dir1', 'dir2/dir3');
  checkEqualTo("fs.read('dir2/dir3/subdir/hello')", 'hello world!');
  fs.rename('dir2/dir3', 'dir1');
  checkThrow(NoSuchEntryError, "fs.rename('no_such_file', 'xxx')");
};


file_test_suite.testQuota = function () {
  var array = [];
  for (var i = 0; i < _core.fsQuota / 2; ++i)
    array.push('x');
  var str = array.join('');
  fs.write('file1', str);
  checkThrow(FSQuotaError, function () { fs.write('file2', str); });
  remove('file1');
  check("!fs.exists('file2')");
};

////////////////////////////////////////////////////////////////////////////////
// Entry point
////////////////////////////////////////////////////////////////////////////////

function main() {
  runTestSuites([
                  base_test_suite,
                  db_test_suite,
                  file_test_suite
                ]);
  return error_count;
}


_core.main = function (expr) {
  return eval(expr);
};


'__main__.js value';
