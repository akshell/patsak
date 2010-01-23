
// (c) 2009-2010 by Anton Korenyushkin

ak.use('ak');


var db = ak.db;
var types = ak.types;
var constrs = ak.constrs;
var fs = ak.fs;
var apps = ak.apps;

var number = ak.db.number;
var string = ak.db.string;
var bool = ak.db.bool;
var date = ak.db.date;


function query(query, query_params, by, by_params, start, length) {
  return db._query(query,
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
  return db._create(name,
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
  ak._print(prefix + descr);
  ++error_count;
}


function check(expr, descr) {
  if (expr instanceof Function)
    check(expr(), '' + expr);
  if (!eval(expr)) {
    descr = descr || expr;
    error(descr + ' check failed');
  }
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
  ak._print(prefix + (error_count ? error_count : 'No') +
            ' errors detected in ' +
            test_count + ' test cases in ' +
            test_suites.length + ' test suites');
  return test_count;
}

////////////////////////////////////////////////////////////////////////////////
// Base test suite
////////////////////////////////////////////////////////////////////////////////

var base_test_suite = {};


var path = ak.path;

base_test_suite.testInclude = function ()  {
  check("path == '__main__.js'");
  check("ak.path === undefined");
  check("ak.include('hello.js') == 'hello'");
  check("ak.include('subdir/another-hello.js') == 'hello'");
  checkThrow(ak.UsageError, "ak.include()");
  checkThrow(ak.NoSuchEntryError, "ak.include('no-such-file.js')");
  checkThrow(SyntaxError, "ak.include('bug.js')");
  checkThrow(SyntaxError, "ak.include('bug-includer.js')");
  checkThrow(ak.PathError, "ak.include('../out-of-base-dir.js')");
  checkThrow(ak.CyclicIncludeError, "ak.include('self-includer.js')");
  checkThrow(ak.CyclicIncludeError, "ak.include('cycle-includer1.js')");
  checkThrow(ak.NoSuchAppError, "ak.include('no_such_lib', 'xxx.js')");
  checkEqualTo("ak.include('lib/0.1/', '/42.js')", 42);
  checkEqualTo("ak.include('lib', '0.1/42.js')", 42);
  ak.include('//subdir///once.js');
  ak.include('subdir/../subdir//./once.js');
  checkThrow(ak.PathError, "ak.include('')");
  checkThrow(ak.PathError, "ak.include('..')");
  checkThrow(ak.PathError, "ak.include('subdir/..')");
  check("ak.include('__main__.js') == '__main__.js value'");
};


base_test_suite.testUse = function () {
  check("ak.use('lib/0.1') == 42");
};


base_test_suite.testType = function () {
  check("number.name == 'number'");
  check("string.name == 'string'");
  check("bool.name == 'boolean'");
  check("date.name == 'date'");
};


base_test_suite.testConstructors = function () {
  check("this instanceof ak.Global");
  check("ak instanceof ak.AK");
  check("db instanceof ak.DB");
  check("keys(ak).indexOf('AK') != -1");
};


base_test_suite.testSetObjectProp = function () {
  var obj = {};
  checkThrow(ak.UsageError, "ak._setObjectProp()");
  checkThrow(TypeError, "ak._setObjectProp(1, 'f', 0, 42)");
  ak._setObjectProp(obj, 'read_only', ak.READ_ONLY, 1);
  obj.setProp('dont_enum', ak.DONT_ENUM, 2);
  obj.setProp('dont_delete', ak.DONT_DELETE, 3);
  checkThrow(TypeError,
             function () { ak._setObjectProp(obj, 'field', {}, 42); });
  checkThrow(ak.UsageError,
             function () { ak._setObjectProp(obj, 'field', 8, 42); });
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
  check("ak.app.name == 'test_app'");
};


base_test_suite.testReadCode = function () {
  check("ak._readCode('subdir/hi.txt') == 'russian привет\\n'");
  check("ak._readCode('bad_app', '__main__.js') == 'wuzzup!!!!!!!!\\n'");
  checkThrow(ak.NoSuchAppError,
             "ak._readCode('illegal/name', 'main.js')");
  checkThrow(ak.PathError, "ak._readCode('test_app', '')");
  checkThrow(ak.PathError,
             "ak._readCode('test_app', 'subdir/../../ak/main.js')");
  checkThrow(ak.UsageError, "ak._readCode()");
};


base_test_suite.testScript = function () {
  check("(new ak.Script('2+2'))._run() === 4");
  check("ak.Script('2+2', 'name')._run() === 4");
  checkThrow(ak.UsageError, "new ak.Script()");
  check(function () {
          try {
            new ak.Script('(');
          } catch (error) {
            return error instanceof SyntaxError;
          }
          return false;
        });
  check(function () {
          try {
            new ak.Script('asdfjkl')._run();
          } catch (error) {
            return error instanceof ReferenceError;
          }
          return false;
        });
  check(function () {
          try {
            new ak.Script('new ak.Script("(")', 'just string')._run();
          } catch (error) {
            return error instanceof SyntaxError;
          }
          return false;
        });
};


base_test_suite.testHash = function () {
  checkThrow(ak.UsageError, "ak._hash()");
  check("ak._hash(undefined) == 0");
  check("ak._hash(null) == 0");
  check("ak._hash(42) == 0");
  check("ak._hash('foo') == 0");
  check("ak._hash('') == 0");
  check("ak._hash({}) > 0");
  check("ak._hash(function () {}) > 0");
};


base_test_suite.testErrors = function () {
  check("new ak.BaseError() instanceof Error");
  check("new ak.CoreError() instanceof ak.BaseError");
  check("ak.CoreError.__name__ == 'ak.CoreError'");
  check("ak.CoreError.prototype.name == 'ak.CoreError'");
  check("new ak.DBError() instanceof ak.CoreError");
  check("new ak.FieldError() instanceof ak.DBError");
  check("ak.UsageError() instanceof ak.UsageError");
  check("ak.BaseError(42).message === '42'");
};


base_test_suite.testConstruct = function () {
  check("typeof(ak._construct(Date, [])) == 'object'");
  checkEqualTo(function () {
                 function C(a, b) {
                   this.sum = a + b;
                 }
                 return ak._construct(C, [1, 2]).sum;
               },
               3);
  checkThrow(ak.UsageError, "ak._construct()");
  checkThrow(ak.UsageError, "ak._construct(42, [])");
  checkThrow(TypeError, "ak._construct(function () {}, 42)");
};

////////////////////////////////////////////////////////////////////////////////
// DB test suite
////////////////////////////////////////////////////////////////////////////////

var db_test_suite = {};


function mapItems(iterable) {
  return map(items, iterable);
}


db_test_suite.setUp = function () {
  db._drop(db._list());

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

  db._insert('User', {name: 'anton', age: 22, flooder: 15});
  db._insert('User', {name: 'marina', age: 25, flooder: false});
  db._insert('User', {name: 'den', age: 23});

  db._insert('Post', {title: 'first', text: 'hello world', author: 0});
  db._insert('Post', {title: 'second', text: 'hi', author: 1});
  db._insert('Post', {title: 'third', text: 'yo!', author: 0});

  db._insert('Comment', {text: 42, author: 1, post: 0});
  db._insert('Comment', {text: 'rrr', author: 0, post: 0});
  db._insert('Comment', {text: 'ololo', author: 2, post: 2});

  create('Count', {i: number});
  for (var i = 0; i < 10; ++i)
    db._insert('Count', {i: i});

  checkEqualTo("db._list().sort()",
               ["Comment", "Count", "Dummy", "Empty", "Post", "User"]);
};


db_test_suite.tearDown = function () {
  db._drop(db._list());
};


db_test_suite.testCreate = function () {
  checkThrow(ak.UsageError, "db._create('illegal', {})");
  checkThrow(ak.UsageError, "create('', {})");
  checkThrow(ak.UsageError, "create('123bad', {})");
  checkThrow(ak.UsageError, "create('illegal', {'_@': number})");
  checkThrow(TypeError, "create('illegal', 'str')");
  checkThrow(TypeError, "db._create('illegal', {}, 'str', [], [])");
  checkThrow(TypeError, "create('illegal', {field: 15})");
  checkThrow(ak.RelVarExistsError, "create('User', {})");

  checkThrow(ak.UsageError,
             function () {
               create('illegal', {x: number}, {unique: [[]]});
             });

  checkThrow(ak.UsageError,
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
  checkThrow(ak.UsageError,
             function () {
               create('illegal',
                      {x: number, y: number},
                      {foreign: [[['x', 'y'],
                                  'Post',
                                  ['id', 'author']]]});
             });
  checkThrow(ak.DBQuotaError,
             function () {
               var name = '';
               for (var i = 0; i < 61; ++i)
                 name += 'x';
               create(name, {});
             });
  checkThrow(ak.DBQuotaError,
             function () {
               attrs = {};
               for (var i = 0; i < 1000; ++i)
                 attrs['attr' + i] = number;
               create('illegal', attrs);
             });
  create('legal', {x: bool._default(new Date())});
  db._insert('legal', {});
  check("query('legal')[0].x === true");
  db._drop(['legal']);
  checkThrow(TypeError, "create('illegal', {x: date._default(42)})");

  checkThrow(ak.UsageError,
             "create('illegal', {}, {foreign: [['a', 'b']]})");
  checkThrow(ak.UsageError,
             function () {
               create('illegal',
                      {a: number},
                      {unique: [['a', 'a']]});
             });
  checkThrow(ak.UsageError,
             function () {
               create('illegal',
                      {x: number},
                      {foreign: [[[], 'User', []]]});
             });
  checkThrow(ak.UsageError,
             function () {
               create('illegal',
                      {x: number},
                      {foreign: [[['x'], 'User', ['age']]]});
             });
  checkThrow(ak.UsageError,
             function () {
               create('illegal',
                      {x: number},
                      {foreign: [[['x'], 'User', ['id', 'age']]]});
             });
};


db_test_suite.testDropRelVars = function () {
  create('NewRelVar', {x: number});
  db._drop(['NewRelVar']);
  check("!('NewRelVar' in db._list())");
  checkThrow(ak.RelVarDependencyError, "db._drop(['User'])");
  checkThrow(ak.RelVarDependencyError, "db._drop(['User', 'Post'])");
  checkThrow(ak.UsageError, "db._drop(['Comment', 'Comment'])");

  create('rv1', {x: number._unique()});
  create('rv2', {x: number._foreign('rv1', 'x')});
  checkThrow(ak.RelVarDependencyError, "db._drop(['rv1'])");
  db._drop(['rv1', 'rv2']);
  checkThrow(ak.NoSuchRelVarError, "db._drop(['Comment', 'no_such'])");
};


db_test_suite.testQuery = function () {
  checkEqualTo("mapItems(query('User[name, age, flooder] where +id == \"0\"'))",
               [[["name", "anton"], ["age", 22], ["flooder", true]]]);
  checkThrow(ak.UsageError, "db._query()");
  checkThrow(TypeError, "query('User', {})");
  checkThrow(TypeError, "query('User', {length: 0.5})");
  checkThrow(TypeError, "query('User', {length: -1})");
  checkThrow(ak.NoSuchRelVarError, "query('dfsa')");
  checkEqualTo(function () {
                 return mapItems(
                   query('Post.author->name where id == $', [0]));
               },
               [[['name', 'anton']]]);
  checkThrow(ak.FieldError, "query('User.asdf')");
  checkEqualTo(function () {
                 return mapItems(
                   query('User[id, name] where id == $1 && name == $2',
                         [0, 'anton']));
               },
               [[['id', 0], ['name', 'anton']]]);
  checkEqualTo("query('User where forsome (x in {}) true').length", 3);
  checkThrow(ak.QueryError, "query('{i: 1} where i->name')");
  checkEqualTo("field('title', ' Post where author->name == $', ['anton'])",
               ['first', 'third']);
  checkEqualTo("field('age', 'User where name == \\'anton\\'')", [22]);
  checkEqualTo("field('age', 'User where name == \"den\"')", [23]);
  checkThrow(ak.QueryError, "query('for (x in {f: 1}) x.f->k')");
  checkThrow(ak.QueryError, "query('{f: 1}', [], ['f->k'])");
};


db_test_suite.testInsert = function () {
  checkThrow(ak.UsageError, "db._insert('User')");
  checkThrow(TypeError, "db._insert('User', 15)");
  checkThrow(ak.FieldError, "db._insert('User', {'@': 'abc'})");
  checkThrow(ak.ConstraintError,
             "db._insert('Comment', {id: 2, text: 'yo', author: 5, post: 0})");
  checkThrow(ak.FieldError,
             "db._insert('User', {id: 2})");
  checkThrow(ak.FieldError,
             "db._insert('Empty', {x: 5})");
  checkEqualTo(
    "items(db._insert('User', {name: 'xxx', age: false}))",
    [['id', 3], ['name', 'xxx'], ['age', 0], ['flooder', true]]);
  var tuple = db._insert('User', {id: 4, name: 'yyy', age: 'asdf'});
  check(isNaN(tuple.age));
  checkThrow(ak.ConstraintError,
             "db._insert('User', {id: 'asdf', name: 'zzz', age: 42})");
  checkThrow(ak.ConstraintError,
             "db._insert('User', {name: 'zzz', age: 42})");
  db._del('User', 'id >= 3', []);
  checkEqualTo("items(db._insert('Empty', {}))", []);
  checkThrow(ak.ConstraintError, "db._insert('Empty', {})");
  db._del('Empty', 'true', []);
};


db_test_suite.testGetHeader = function () {
  checkEqualTo("items(db._getHeader('User')).sort()",
               [['age', 'number'],
                ['flooder', 'boolean'],
                ['id', 'serial'],
                ['name', 'string']]);
};


db_test_suite.testBy = function () {
  create('ByTest', {x: number, y: number});
  db._insert('ByTest', {x: 0, y: 0});
  db._insert('ByTest', {x: 1, y: 14});
  db._insert('ByTest', {x: 2, y: 21});
  db._insert('ByTest', {x: 3, y: 0});
  db._insert('ByTest', {x: 4, y: 0});
  db._insert('ByTest', {x: 5, y: 5});
  db._insert('ByTest', {x: 8, y: 3});
  db._insert('ByTest', {x: 9, y: 32});
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
  db._drop(['ByTest']);
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
  checkEqualTo("db._count('User', [])", 3);
  checkEqualTo("db._count('union({i: 1}, {i: 2}, {i: 3})', [])", 3);
  checkEqualTo(
    "db._count('Post.author where id < 2 && author->flooder', [])",
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
  checkThrow(ak.UsageError, "db._update('User', 'id == 0', [], {}, [])");
  checkThrow(ak.UsageError, "db._update('User', 'id == 0', [], {})");
  checkThrow(TypeError, "db._update('User', 'id == 0', [], 1, [])");
  checkThrow(ak.ConstraintError,
             "db._update('User', 'id == 0', [], {id: '$'}, ['asdf'])");
  checkEqualTo("db._update('User', 'id == 0', [], {name: '$'}, ['ANTON'])",
               1);
  checkEqualTo("field('name', 'User where id == 0')", ['ANTON']);
  var rows_number = db._update('User',
                               'name != $',
                               ['marina'],
                               {age: 'age + $1', flooder: 'flooder || $2'},
                               [2, 'yo!']);
  check(rows_number == 2);
  for (var i = 0; i < 10; ++i)
    checkThrow(
      ak.ConstraintError,
      "db._update('User', 'name == $', ['den'], {id: '4'}, [])");
  forEach(initial, function (tuple) {
            db._update('User', 'id == $', [tuple.id],
                       {name: '$1', age: '$2', flooder: '$3'},
                       [tuple.name, tuple.age, tuple.flooder]);
          });
  checkEqualTo("mapItems(query('User'))", mapItems(initial));
};


db_test_suite.testDel = function () {
  checkThrow(ak.ConstraintError, "db._del('User', 'true', [])");
  var tricky_name = 'xx\'y\'zz\'';
  db._insert('User', {id: 3, name: tricky_name, age: 15, flooder: true});
  db._insert('Post', {id: 3, title: "", text: "", author: 3});
  db._update('User', 'id == 3', [], {name: 'name + 1'}, []);
  checkEqualTo("field('name', 'User where id == 3')", [tricky_name + 1]);
  checkEqualTo("db._del('Post', 'author->name == $', ['xx\\'y\\'zz\\'1'])", 1);
  checkEqualTo("db._del('User', 'age == $', [15])", 1);
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
  db._insert('pg_class', {x: 0});
  checkEqualTo(function () { return mapItems(query('pg_class')); },
               [[['x', 0]]]);
  db._drop(['pg_class']);
};


db_test_suite.testCheck = function () {
  create('silly', {n: number._check('n != 42')});
  create('dummy', {b: bool, s: string}, {check: ['b || s == "hello"']});
  db._insert('silly', {n: 0});
  checkThrow(ak.ConstraintError, "db._insert('silly', {n: 42})");
  db._insert('dummy', {b: true, s: 'hi'});
  db._insert('dummy', {b: false, s: 'hello'});
  checkThrow(ak.ConstraintError, "db._insert('dummy', {b: false, s: 'oops'})");
  db._drop(['silly', 'dummy']);
};


db_test_suite.testDate = function () {
  create('d1', {d: date}, {unique: [['d']]});
  var some_date = new Date(Date.parse('Wed, Mar 04 2009 16:12:09 GMT'));
  var other_date = new Date(2009, 0, 15, 13, 27, 11, 481);
  db._insert('d1', {d: some_date});
  checkEqualTo("field('d', 'd1')", [some_date]);
  create('d2', {d: date}, {foreign: [[['d'], 'd1', ['d']]]});
  checkThrow(ak.ConstraintError,
             function () { db._insert('d2', {d: other_date}); });
  db._insert('d1', {d: other_date});
  checkEqualTo("field('d', 'd1', [], ['-d'])", [some_date, other_date]);
  db._insert('d2', {d: other_date});
  db._drop(['d1', 'd2']);
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
  checkEqualTo("items(db._getDefault('def')).sort()",
               [['b', true],
                ['d', now],
                ['n', 42],
                ['s', 'hello, world!']]);
  db._insert('def', {});
  checkThrow(ak.ConstraintError, "db._insert('def', {})");
  db._insert('def', {b: false});
  db._insert('def', {n: 0, s: 'hi'});
  checkEqualTo(function () { return mapItems(query('def', [], ['b', 'n'])); },
               [[['n', 42], ['s', 'hello, world!'], ['b', false], ['d', now]],
                [['n', 0], ['s', 'hi'], ['b', true], ['d', now]],
                [['n', 42], ['s', 'hello, world!'], ['b', true], ['d', now]]]);
  db._drop(['def']);
};


db_test_suite.testIntegerSerial = function () {
  checkThrow(ak.UsageError, "number._serial()._default(42)");
  checkThrow(ak.UsageError, "number._default(42)._serial()");
  checkThrow(ak.UsageError, "number._serial()._integer()");
  checkThrow(ak.UsageError, "number._serial()._serial()");
  checkThrow(ak.UsageError, "number._integer()._integer()");
  checkEqualTo("db._getInteger('Comment').sort()", ['author', 'id', 'post']);
  create('rv',
         {
           x: number._serial(),
           y: number._serial(),
           z: number._integer()
         });
  checkEqualTo("db._getInteger('rv').sort()", ['x', 'y', 'z']);
  checkEqualTo("db._getSerial('rv').sort()", ['x', 'y']);
  db._drop(['rv']);
};


db_test_suite.testUnique = function () {
  create('rv',
         {a: number, b: string, c: bool},
         {unique: [['a', 'b'], ['b', 'c'], ['c']]});
  checkEqualTo("db._getUnique('rv').sort()",
               [['a', 'b'], ['a', 'b', 'c'], ['b', 'c'], ['c']]);
  checkEqualTo("db._getUnique('Dummy')", [['id']]);
  db._drop(['rv']);
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
  checkEqualTo("db._getForeign('rv').sort()",
               [
                 [["ref"], "rv", ["id"]],
                 [["title", "author"], "Post", ["title", "author"]]
               ]);
  db._drop(['rv']);
};


db_test_suite.testRelVarNumber = function () {
  checkThrow(ak.DBQuotaError,
             function () {
               for (var i = 0; i < 500; ++i)
                 create('rv' + i, {});
             });
  checkThrow(ak.NoSuchRelVarError,
             function () {
               for (var i = 0; i < 500; ++i)
                 db._drop(['rv' + i]);
             });
};


db_test_suite.testQuota = function () {
  check("ak.dbQuota > 0");
  create('rv', {i: number._integer(), s: string});
  var array = [];
  for (var i = 0; i < 100 * 1024; ++i)
    array.push('x');
  var str = array.join('');
  checkThrow(ak.ConstraintError,
             function () { db._insert('rv', {i: 0, s: str + 'x'}); });
  // Slow DB size quota test, uncomment to run
  //   checkThrow(ak.DBQuotaError,
  //   function () {
  //     for (var i = 0; ; ++i)
  //       db._insert('rv', {i: i, s: str});
  //   });
  db._drop(['rv']);
};


db_test_suite.testDescribeApp = function () {
  checkEqualTo(items(db._describeApp('test_app')),
               [['admin', 'test_user'],
                ['developers', ['Odysseus', 'Achilles']],
                ['email', 'a@b.com'],
                ['summary', 'test app'],
                ['description', 'test app...'],
                ['labels', ['1', '2']]]);
  checkEqualTo(items(db._describeApp('another_app')),
               [["admin", "Odysseus"],
                ["developers", []],
                ["email", "x@y.com"],
                ["summary", "another app"],
                ["description", "another app..."],
                ["labels", ["1"]]]);
  checkThrow(ak.NoSuchAppError, "db._describeApp('no_such_app')");
  checkThrow(ak.UsageError, "db._describeApp()");
};


db_test_suite.testGetAdminedApps = function () {
  checkEqualTo(db._getAdminedApps('test_user').sort(),
               ['ak', 'bad_app', 'blocking_app',
                'lib', 'test_app', 'throwing_app']);
  checkEqualTo(db._getAdminedApps('Achilles'), []);
  checkThrow(ak.NoSuchUserError,
             function () { db._getAdminedApps('no_such_user'); });

};


db_test_suite.testGetDevelopedApps = function () {
  checkEqualTo(db._getDevelopedApps('Odysseus').sort(), ['ak', 'test_app']);
  checkEqualTo(db._getDevelopedApps('test_user'), []);
  checkThrow(ak.NoSuchUserError,
             function () { db._getDevelopedApps('no_such_user'); });
};


db_test_suite.testGetAppsByLabel = function () {
  checkEqualTo(db._getAppsByLabel('1').sort(), ['another_app', 'test_app']);
  checkEqualTo(db._getAppsByLabel('2'), ['test_app']);
  checkEqualTo(db._getAppsByLabel('no_such_label'), []);
};

////////////////////////////////////////////////////////////////////////////////
// File test suite
////////////////////////////////////////////////////////////////////////////////

var file_test_suite = {};


file_test_suite.setUp = function ()
{
  forEach(ak.fs._list(''), ak.fs.remove);
  fs._makeDir('dir1');
  fs._makeDir('dir2');
  fs._makeDir('dir1/subdir');
  fs._write('file', 'some text');
  fs._write('dir1/subdir/hello', 'hello world!');
  fs._write('dir1/subdir/привет', 'привет!');
};


file_test_suite.tearDown = function () {
  forEach(ak.fs._list(''), ak.fs.remove);
};


file_test_suite.testRead = function () {
  checkEqualTo("fs._read('//dir1////subdir/hello')",
               'hello world!');
  var data = fs._read('/dir1/subdir/привет');
  checkEqualTo(data, 'привет!');
  checkThrow(ak.ConversionError, function () { data.toString('UTF-32'); });
  checkThrow(ak.UsageError, function () { data.toString('NO_SUCH_ENC'); });
  checkThrow(ak.NoSuchEntryError, "fs._read('does_not_exists')");
  checkThrow(ak.EntryIsDirError, "fs._read('dir1')");
  checkThrow(ak.PathError, "fs._read('//..//test_app/dir1/subdir/hello')");
  checkThrow(ak.PathError, "fs._read('/dir1/../../file')");
  checkThrow(ak.PathError, "fs._read('////')");
  checkThrow(ak.PathError,
             function () {
               var path = '';
               for (var i = 0; i < 40; ++i)
                 path += '/dir';
               fs._read(path);
             });
};


file_test_suite.testList = function () {
  checkEqualTo("fs._list('').sort()", ['dir1', 'dir2', 'file']);
  checkThrow(ak.NoSuchEntryError, "fs._list('no_such_dir')");
};


file_test_suite.testExists = function () {
  checkEqualTo("fs._exists('')", true);
  checkEqualTo("fs._exists('dir1/subdir/hello')", true);
  checkEqualTo("fs._exists('no/such')", false);
};


file_test_suite.testIsDir = function () {
  checkEqualTo("fs._isDir('')", true);
  checkEqualTo("fs._isDir('dir2')", true);
  checkEqualTo("fs._isDir('file')", false);
  checkEqualTo("fs._isDir('no/such')", false);
};


file_test_suite.testIsFile = function () {
  checkEqualTo("fs._isFile('')", false);
  checkEqualTo("fs._isFile('dir1/subdir/hello')", true);
  checkEqualTo("fs._isFile('dir1/subdir')", false);
  checkEqualTo("fs._isFile('no/such')", false);
};


file_test_suite.testRemove = function () {
  fs._write('new-file', 'data');
  fs._remove('new-file');
  fs._makeDir('dir2/new-dir');
  fs._remove('dir2/new-dir');
  checkEqualTo("fs._list('').sort()", ['dir1', 'dir2', 'file']);
  checkEqualTo("fs._list('dir2')", []);
  checkThrow(ak.DirIsNotEmptyError, "fs._remove('dir1')");
};


file_test_suite.testWrite = function () {
  fs._write('wuzzup', 'yo wuzzup!');
  checkEqualTo("fs._read('wuzzup')", 'yo wuzzup!');
  fs._write('hello', fs._read('dir1/subdir/hello'));
  checkEqualTo("fs._read('hello')", 'hello world!');
  fs._remove('wuzzup');
  checkThrow(ak.EntryIsNotDirError, "fs._write('file/xxx', '')");
  checkThrow(ak.PathError,
             function () {
               var array = [];
               for (var i = 0; i < 1000; ++i)
                 array.push('x');
               fs._write(array.join(''), '');
             });
};


file_test_suite.testMakeDir = function () {
  fs._makeDir('dir2/ddd');
  checkEqualTo("fs._list('dir2')", ['ddd']);
  checkEqualTo("fs._list('dir2/ddd')", []);
  fs._remove('dir2/ddd');
  checkThrow(ak.EntryExistsError, "fs._makeDir('file')");
};


file_test_suite.testCopyFile = function () {
  fs._copyFile('dir1/subdir/hello', 'dir2/hello');
  checkEqualTo("fs._read('dir2/hello')", 'hello world!');
  fs._remove('dir2/hello');
  checkThrow(ak.NoSuchEntryError, "fs._copyFile('no_such', 'never_created')");
  checkThrow(ak.EntryIsDirError, "fs._copyFile('file', 'dir1/subdir')");
};


file_test_suite.testRename = function () {
  fs._rename('dir1', 'dir2/dir3');
  checkEqualTo("fs._read('dir2/dir3/subdir/hello')", 'hello world!');
  fs._rename('dir2/dir3', 'dir1');
  checkThrow(ak.NoSuchEntryError, "fs._rename('no_such_file', 'xxx')");
};


file_test_suite.testQuota = function () {
  var array = [];
  for (var i = 0; i < ak.fsQuota / 2; ++i)
    array.push('x');
  var str = array.join('');
  fs._write('file1', str);
  checkThrow(ak.FSQuotaError, function () { fs._write('file2', str); });
  fs._remove('file1');
  check("!fs._exists('file2')");
};

////////////////////////////////////////////////////////////////////////////////
// requestApp test suite
////////////////////////////////////////////////////////////////////////////////

var request_app_test_suite = {};

request_app_test_suite.testRequest = function ()
{
  fs._write('file1', 'wuzzup');
  fs._write('file2', 'yo ho ho');
  fs._write('hello', 'hello world!');
  checkEqualTo(function () {
                 return ak._requestApp('another_app',
                                       fs._read('hello'),
                                       ['file1', 'file2'],
                                       'yo!!!');
               },
               '{"user":"' + ak._user + '",'+
               '"arg":"hello world!","data":"yo!!!",' +
               '"file_contents":["wuzzup","yo ho ho"],' +
               '"issuer":"test_app"}');
  check("!fs._exists('file1') && !fs._exists('file2')");
  fs.remove('hello');
  checkThrow(ak.NoSuchAppError,
             "ak._requestApp('no_such_app', 'hi', [], null)");
  checkThrow(TypeError, "ak._requestApp('another_app', '', 42, null)");
  checkEqualTo(
    function () {
      fs._write('file3', 'text');
      var result = ak._requestApp('another_app', '', [], fs._read('file3'));
      fs.remove('file3');
      return result;
    },
    '{"user":"' + ak._user + '",' +
      '"arg":"",' +
      '"data":"text",' +
      '"file_contents":[],' +
      '"issuer":"test_app"}');
  checkThrow(ak.NoSuchAppError,
             "ak._requestApp('invalid/app/name', '', [], null)");
  checkThrow(ak.SelfRequestError,
             "ak._requestApp('test_app', '2+2', [], null)");
  checkThrow(ak.ProcessingFailedError,
             "ak._requestApp('throwing_app', '', [], null)");
  checkThrow(ak.TimedOutError, "ak._requestApp('blocking_app', '', [], null)");
  checkThrow(ak.PathError, "ak._requestApp('another_app', '', ['..'], null)");
  checkThrow(ak.NoSuchEntryError,
             "ak._requestApp('another_app', '', ['no-such-file'], null)");
  checkThrow(ak.EntryIsDirError,
             function () {
               fs._makeDir('dir');
               try {
                 ak._requestApp('another_app', '', ['dir'], null);
               } finally {
                 fs.remove('dir');
               }
             });
  checkThrow(TypeError, "ak._requestApp('another_app', 'hi', 42, null)");
};

////////////////////////////////////////////////////////////////////////////////
// Entry point
////////////////////////////////////////////////////////////////////////////////

function main() {
  runTestSuites([
                  base_test_suite,
                  db_test_suite,
                  file_test_suite,
                  request_app_test_suite
                ]);
  return error_count;
}


ak._main = function (expr) {
  return eval(expr);
};


'__main__.js value';
