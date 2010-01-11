
// (c) 2009-2010 by Anton Korenyushkin

ak.use('ak');


var db = ak.db;
var types = ak.types;
var constrs = ak.constrs;
var fs = ak.fs;
var apps = ak.apps;

var number = ak._dbMediator.number;
var string = ak._dbMediator.string;
var bool = ak._dbMediator.bool;
var date = ak._dbMediator.date;

[
  'query',
  'makeRelVar',
  'dropRelVars',
  'unique',
  'foreign',
  'check',
  'describeApp',
  'getAdminedApps',
  'getDevelopedApps',
  'getAppsByLabel'
].map(function (name) {
        ak[name] = function () {
          return ak._dbMediator['_' + name].apply(ak._dbMediator,
                                                  arguments);
        };
      });


function print(x) {
  ak._print(x);
}


function println(x) {
  ak._print(x + '\n');
}

////////////////////////////////////////////////////////////////////////////////
// Test tools
////////////////////////////////////////////////////////////////////////////////

var error_count;
var prefix = '### ';


function error(descr) {
  print(prefix + descr + '\n');
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
  for (field in test_suite) {
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
        test_suites.length + ' test suites\n');
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
  check("keys(ak).indexOf('_DBMediator') == -1");
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
  checkThrow(ak.UsageError, "ak._construct(function () {}, 42)");
};

////////////////////////////////////////////////////////////////////////////////
// DB test suite
////////////////////////////////////////////////////////////////////////////////

var db_test_suite = {};

function mapItems(iterable) {
  return map(items, iterable);
}


db_test_suite.setUp = function () {
  ak.dropRelVars(keys(db));

  db.Dummy._create({id: number});
  db.Empty._create({});
  db.User._create({id: number._serial()._unique(),
                   name: string._unique(),
                   age: number,
                   flooder: bool._default('yo!')});
  db.Post._create({id: number._serial()._unique(),
                   title: string,
                   text: string,
                   author: number._integer()._foreign('User', 'id')},
                  ak.unique(['title', 'author']));
  db.Comment._create({id: number._serial()._unique(),
                      text: string,
                      author: number._integer()._foreign('User', 'id'),
                      post: number._integer()._foreign('Post', 'id')});

  db.User._insert({name: 'anton', age: 22, flooder: 15});
  db.User._insert({name: 'marina', age: 25, flooder: false});
  db.User._insert({name: 'den', age: 23});

  db.Post._insert({title: 'first', text: 'hello world', author: 0});
  db.Post._insert({title: 'second', text: 'hi', author: 1});
  db.Post._insert({title: 'third', text: 'yo!', author: 0});

  db.Comment._insert({text: 42, author: 1, post: 0});
  db.Comment._insert({text: 'rrr', author: 0, post: 0});
  db.Comment._insert({text: 'ololo', author: 2, post: 2});

  db.Count._create({i: number});
  for (var i = 0; i < 10; ++i)
    db.Count._insert({i: i});
};


db_test_suite.tearDown = function () {
  ak.dropRelVars(keys(db));
};


db_test_suite.testConstructors = function () {
  var q = ak.query('User');
  check(q instanceof ak.Rel, "ak.Rel");
};


db_test_suite.testRelVarCreate = function () {
  checkThrow(ak.UsageError, "db.illegal._create()");
  checkThrow(TypeError, "db.illegal._create('str')");
  checkThrow(TypeError, "db.illegal_create({field: 15})");
  checkThrow(ak.UsageError, "db.$");
  checkThrow(ak.UsageError, "db['1a']");
  checkThrow(ak.UsageError, "db['ab#cd']");
  checkThrow(ak.UsageError, "db['']");
  checkThrow(ak.RelVarExistsError, "db.User._create({})");
  checkThrow(TypeError,
             function () {
               var obj = {length: 15};
               db.illegal._create({x: number}, ak.unique(obj));
             });

  var obj = {toString: function () { return 'x'; }};
  db.legal._create({x: number}, ak.unique(obj));
  db.legal._drop();

  obj.length = 12.1;
  db.legal._create({x: number}, ak.unique(obj));
  db.legal._drop();

  obj.length = -1;
  db.legal._create({x: number}, ak.unique(obj));
  db.legal._drop();

  checkThrow(ak.UsageError,
             function () {
               db.illegal._create({x: number}, ak.unique([]));
             });

  checkThrow(ak.UsageError,
             function () {
               db.illegal._create({x: number, y: number},
                                  ak.foreign(['x', 'y'], 'User', 'id'));
             });

  checkThrow(TypeError,
             function () {
               var obj = {length: 1};
               db.illegal._create({undefined: number},
                                  ak.foreign(obj, 'Post', 'id'));
             });
  checkThrow(ak.UsageError,
             function () {
               db.illegal._create({x: number, y: number},
                                  ak.foreign(['x', 'y'],
                                             'Post',
                                             ['id', 'author']));
             });
  checkThrow(ak.DBQuotaError,
             function () {
               var name = '';
               for (var i = 0; i < 61; ++i)
                 name += 'x';
               db[name]._create({});
             });
  checkThrow(ak.DBQuotaError,
             function () {
               attrs = {};
               for (var i = 0; i < 1000; ++i)
                 attrs['attr' + i] = number;
               db.illegal._create(attrs);
             });
  db.legal._create({x: bool._default(new Date())});
  db.legal._insert({});
  check("db.legal._all()[0].x === true");
  db.legal._drop();
  checkThrow(TypeError, "db.illegal._create({x: date._default(42)})");
};


db_test_suite.testConstr = function () {
  checkThrow(ak.UsageError,
             "db.illegal._create({}, ak.unique())");
  checkThrow(ak.UsageError,
             "ak.foreign('a', 'b')");
  checkThrow(ak.UsageError,
             "ak.unique(['a', 'a'])");
  checkThrow(ak.UsageError,
             "ak.unique('a', 'a')");
  ak.check('field != 0');
  checkThrow(TypeError, "db.illegal._create({x: number}, {})");
  checkThrow(ak.UsageError,
             function () {
               db.illegal._create({x: number},
                                  ak.foreign([], 'User', []));
             });
  checkThrow(ak.UsageError,
             function () {
               db.illegal._create({x: number},
                                  ak.foreign('x', 'User', 'age'));
             });
};


db_test_suite.testDropRelVars = function () {
  db.NewRelVar._create({x: number});
  db.NewRelVar._drop();
  check("!('NewRelVar' in db)");
  checkThrow(ak.RelVarDependencyError, "db.User._drop()");
  checkThrow(ak.RelVarDependencyError, "ak.dropRelVars('User', 'Post')");
  checkThrow(ak.UsageError,
             "ak.dropRelVars('Comment', 'Comment')");
  checkThrow(ak.UsageError,
             "ak.dropRelVars(['Comment', 'Comment'])");

  db.rel1._create({x: number}, ak.unique('x'));
  db.rel2._create({x: number}, ak.foreign('x', 'rel1', 'x'));
  checkThrow(ak.RelVarDependencyError, "db.rel1._drop()");
  ak.dropRelVars('rel1', 'rel2');
  checkThrow(ak.NoSuchRelVarError, "ak.dropRelVars('Comment', 'no_such')");
};


db_test_suite.testRel = function () {
  var q = ak.query('User[name, age, flooder] where +id == "0"');
  checkEqualTo(function () { return items(q[0]); },
               [['name', 'anton'], ['age', 22], ['flooder', true]]);
  q[1] = 42;
  check(function () { return q[1] === undefined; });
  q.length = 42;
  check(function () { return q.length == 1; });
  delete q.length;
  check(function () { return q.length == 1; });
  check(function () { return (0 in q) && !(1 in q); });
  checkEqualTo(function () { return keys(q); }, [0]);
  checkThrow(ak.UsageError, "ak.query()");
  checkThrow(ak.NoSuchRelVarError, "ak.query('dfsa')._perform()");
  checkThrow(ak.NoSuchRelVarError, "ak.query('dfsa').length");
  checkThrow(ak.NoSuchRelVarError, "ak.query('dfsa')[0]");
  check("!(0 in ak.query('dfsa'))");
  check("compare(keys(ak.query('dfsa')), []) == 0");
  check("ak.query('User')._perform() === undefined");
  checkEqualTo(function () {
                 return mapItems(
                   ak.query('Post.author->name where id == $', 0));
               },
               [[['name', 'anton']]]);
  checkThrow(ak.FieldError, "ak.query('User.asdf')._perform()");
};


db_test_suite.testInsert = function () {
  checkThrow(ak.UsageError, "db.User._insert()");
  checkThrow(TypeError, "db.User._insert(15)");
  checkThrow(ak.UsageError, "db.User._insert({'@': 'abc'})");
  checkThrow(ak.ConstraintError,
             "db.Comment._insert({id: 2, text: 'yo', author: 5, post: 0})");
  checkThrow(ak.FieldError,
             "db.User._insert({id: 2})");
  checkThrow(ak.FieldError,
             "db.Empty._insert({x: 5})");
  checkEqualTo(
    "items(db.User._insert({name: 'xxx', age: false}))",
    [['id', 3], ['name', 'xxx'], ['age', 0], ['flooder', true]]);
  var tuple = db.User._insert({id: 4, name: 'yyy', age: 'asdf'});
  check(isNaN(tuple.age));
  checkThrow(ak.ConstraintError,
             "db.User._insert({id: 'asdf', name: 'zzz', age: 42})");
  checkThrow(ak.ConstraintError,
             "db.User._insert({name: 'zzz', age: 42})");
  db.User._where('id >= 3')._del();
  checkEqualTo("items(db.Empty._insert({}))", []);
  checkThrow(ak.ConstraintError, "db.Empty._insert({})");
  db.Empty._all()._del();
};


db_test_suite.testRelVar = function () {
  check("db.User.name == 'User'");
  checkEqualTo("items(db.User.header).sort()",
               [['age', 'number'],
                ['flooder', 'boolean'],
                ['id', 'serial'],
                ['name', 'string']]);
  check("'name' in db.User");
  check("'header' in db.User");
  check("'_insert' in db.User");
};


db_test_suite.testDB = function () {
  check("'Comment' in db");
  check("!('second' in db)");
  checkEqualTo("keys(db).sort()",
               ['Comment', 'Count', 'Dummy', 'Empty', 'Post', 'User']);
  db.x = 42;
  check("!('x' in db)");
  check("db.x instanceof ak.RelVar");
};


db_test_suite.testWhere = function () {
  checkEqualTo(function () {
                 return mapItems(
                   ak.query('User[id, name]')
                     ._where('id == $1 && name == $2', 0, 'anton'));
               },
               [[['id', 0], ['name', 'anton']]]);
  checkThrow(ak.UsageError, "ak.query('User')._where()");
  checkEqualTo("db.User._where('forsome (x in {}) true').length", 3);
  checkEqualTo("db.User._where('true').relVar.name", 'User');
  checkThrow(ak.QueryError, "ak.query('{i: 1}')._where('!i->name')._perform()");
  checkEqualTo(("ak.query(' Post ')" +
                "._where('author->name == $', 'anton')" +
                ".field('title')"),
               ['first', 'third']);
};


db_test_suite.testBy = function () {
  db.ByTest._create({x: number, y: number});
  db.ByTest._insert({x: 0, y: 1});
  db.ByTest._insert({x: 1, y: 7});
  db.ByTest._insert({x: 2, y: 3});
  db.ByTest._insert({x: 3, y: 9});
  db.ByTest._insert({x: 4, y: 4});
  db.ByTest._insert({x: 5, y: 2});
  checkEqualTo(("ak.query('ByTest')._where('y != $', 9)" +
                "._by('x * $1 % $2', 2, 7).field('y')"),
               [1, 4, 7, 2, 3]);
  check("db.ByTest._by('x') instanceof ak.Selection");
  db.ByTest._drop();
};


db_test_suite.testOnly = function () {
  checkThrow(TypeError,
             function () {
               var obj = {length: 1};
               ak.query('User')._only(obj);
             });
  check("db.User._only('name') instanceof ak.Selection");
};


db_test_suite.testSubrel = function () {
  checkEqualTo("db.Count._subrel(8).field('i')", [8, 9]);
  checkEqualTo("db.Count._subrel(1, 8)._subrel(2, 5)._subrel(3, 4).field('i')",
               [6, 7]);
  checkEqualTo("db.Count._subrel(10)", []);
  checkEqualTo("db.Count._subrel(0, 5)._subrel(6)", []);
  checkEqualTo(("db.Count" +
                "._subrel(0, 7)" +
                "._only('i')" +
                "._where('i != $', 5)" +
                "._by('i + $', 1)" +
                "._subrel(1, 10)" +
                "._count()"),
               5);
  checkThrow(TypeError, "db.Count._subrel(-1)");
};


db_test_suite.testCount = function () {
  checkEqualTo("db.User._count()", 3);
  checkEqualTo("db.User._subrel(1, 3)._count()", 2);
  checkEqualTo("ak.query('union({i: 1}, {i: 2}, {i: 3})')._subrel(1, 3)._count()", 2);
  checkEqualTo(("ak.query('Post.author->[id, flooder] where id < 2')" +
                "._where('flooder')._count()"),
               1);
};


db_test_suite.testAll = function () {
  check("db.User._all().field('id').sort()", [0, 1, 2]);
  check("db.User._all()._where('!(id % 2)')._by('-id').field('name')",
        ['den', 'anton']);
  check("db.User._where('id == $', 0)[0]['name']",
        'anton');
  check("db.User._where('name != $', 'den')._by('flooder').field('id')",
        [1, 0]);
};


db_test_suite.testUpdate = function () {
  var initial = db.User._all();
  initial._perform();
  checkThrow(ak.UsageError, "db.User._where('id == 0')._update({})");
  checkThrow(ak.UsageError, "db.User._where('id == 0')._update()");
  checkThrow(TypeError, "db.User._where('id == 0')._update(1)");
  checkThrow(ak.ConstraintError,
             "db.User._where('id == 0')._update({id: '$'}, 'asdf')");
  checkEqualTo("db.User._where('id == 0')._update({name: '$'}, 'ANTON')",
               1);
  check("db.User._where('id == 0')[0]['name'] == 'ANTON'");
  var rows_number = db.User
    ._where('name != $', 'marina')
    ._by('id')._update({age: 'age + $1',
                        flooder: 'flooder || $2'},
                       2,
                       'yo!');
  check(rows_number == 2);
  for (var i = 0; i < 10; ++ i)
    checkThrow(
      ak.ConstraintError,
      "db.User._where('name == $', 'den')._update({id: '4'})");
  forEach(initial, function (tuple) {
            db.User._where('id == $', tuple.id)._update(
              {name: '$1', age: '$2', flooder: '$3'},
              tuple.name, tuple.age, tuple.flooder);
          });
  checkEqualTo(function () { return mapItems(db.User._all()); },
               mapItems(initial));
};


db_test_suite.testDelete = function () {
  var initial = db.User._all();
  initial._perform();
  checkThrow(ak.ConstraintError, "db.User._all()._del()");
  var tricky_name = 'xx\'y\'zz\'';
  db.User._insert({id: 3, name: tricky_name, age: 15, flooder: true});
  db.User._by('name')._where('id == 3')._update({name: 'name + 1'});
  checkEqualTo("db.User._where('id == 3')[0]['name']", tricky_name + 1);
  checkEqualTo("db.User._where('age == 15')._del()", 1);
  checkEqualTo("db.User.field('id').sort()", [0, 1, 2]);
};


db_test_suite.testStress = function () {
  for (var i = 0; i < 10; ++i) {
    checkEqualTo(("db.Comment" +
                  "._where('post->author->name == $', 'anton')" +
                  "._where('author->flooder')" +
                  "._by('author->age')" +
                  ".field('text')"),
                 ['rrr', 'ololo']);
    this.testUpdate();
    this.testDelete();
    this.testSubrel();
  };
};


db_test_suite.testPg = function () {
  db.pg_class._create({x: number});
  db.pg_class._insert({x: 0});
  checkEqualTo(function () { return mapItems(db.pg_class._all()); },
               [[['x', 0]]]);
  db.pg_class._drop();
};


db_test_suite.testCheck = function () {
  db.silly._create({n: number._check('n != 42')});
  db.dummy._create({b: bool, s: string},
                   ak.check('b || s == "hello"'));
  db.silly._insert({n: 0});
  checkThrow(ak.ConstraintError, "db.silly._insert({n: 42})");
  db.dummy._insert({b: true, s: 'hi'});
  db.dummy._insert({b: false, s: 'hello'});
  checkThrow(ak.ConstraintError, "db.dummy._insert({b: false, s: 'oops'})");
  db.silly._drop();
  db.dummy._drop();
};


db_test_suite.testDate = function () {
  db.d1._create({d: date}, ak.unique('d'));
  var some_date = new Date(Date.parse('Wed, Mar 04 2009 16:12:09 GMT'));
  var other_date = new Date(2009, 0, 15, 13, 27, 11, 481);
  db.d1._insert({d: some_date});
  checkEqualTo("db.d1.field('d')", [some_date]);
  db.d2._create({d: date}, ak.foreign('d', 'd1', 'd'));
  checkThrow(ak.ConstraintError,
             function () { db.d2._insert({d: other_date}); });
  db.d1._insert({d: other_date});
  checkEqualTo("db.d1._by('-d').field('d')", [some_date, other_date]);
  db.d2._insert({d: other_date});
  ak.dropRelVars('d1', 'd2');
};


db_test_suite.testDefault = function () {
  var now = new Date();
  db.def._create({n: number._default(42),
                  s: string._default('hello, world!'),
                  b: bool._default(true),
                  d: date._default(now)});
  checkEqualTo("items(db.def._getDefaults()).sort()",
               [['b', true],
                ['d', now],
                ['n', 42],
                ['s', 'hello, world!']]);
  db.def._insert({});
  checkThrow(ak.ConstraintError, "db.def._insert({})");
  db.def._insert({b: false});
  db.def._insert({n: 0, s: 'hi'});
  checkEqualTo(function () { return mapItems(db.def._by('b')._by('n')); },
               [[['n', 42], ['s', 'hello, world!'], ['b', false], ['d', now]],
                [['n', 0], ['s', 'hi'], ['b', true], ['d', now]],
                [['n', 42], ['s', 'hello, world!'], ['b', true], ['d', now]]]);
  db.def._drop();
};


db_test_suite.testIntegerSerial = function () {
  checkThrow(ak.UsageError, "number._serial()._default(42)");
  checkThrow(ak.UsageError, "number._default(42)._serial()");
  checkThrow(ak.UsageError, "number._serial()._integer()");
  checkThrow(ak.UsageError, "number._serial()._serial()");
  checkThrow(ak.UsageError, "number._integer()._integer()");
  checkEqualTo("db.Comment._getIntegers().sort()", ['author', 'id', 'post']);
  db.r._create({x: number._serial(),
                y: number._serial(),
                z: number._integer()});
  checkEqualTo("db.r._getIntegers().sort()", ['x', 'y', 'z']);
  checkEqualTo("db.r._getSerials().sort()", ['x', 'y']);
  db.r._drop();
};


db_test_suite.testUnique = function () {
  db.r._create({a: number, b: string, c: bool},
               ak.unique('a', 'b'),
               ak.unique('b', 'c'),
               ak.unique('c'));
  checkEqualTo("db.r._getUniques().sort()",
               [['a', 'b'], ['a', 'b', 'c'], ['b', 'c'], ['c']]);
  checkEqualTo("db.Dummy._getUniques()", [['id']]);
  db.r._drop();
};


db_test_suite.testForeignKey = function () {
  db.r._create({title: string,
                author: number._integer(),
                id: number._serial(),
                ref: number._integer()},
               ak.foreign(['title', 'author'],
                          'Post',
                          ['title', 'author']),
               ak.foreign('ref', 'r', 'id'),
               ak.unique('id'));
  checkEqualTo(function () {
                 return map(function (fk) {
                              return items(fk).sort();
                            },
                            db.r._getForeigns()).sort();
               },
               [[["keyFields", ["ref"]],
                 ["refFields", ["id"]],
                 ["refRelVar", "r"]],
                [["keyFields", ["title", "author"]],
                 ["refFields", ["title", "author"]],
                 ["refRelVar", "Post"]]]);
  db.r._drop();
};


db_test_suite.testRelVarNumber = function () {
  checkThrow(ak.DBQuotaError,
             function () {
               for (var i = 0; i < 500; ++i)
                 db['r' + i]._create({});
             });
  checkThrow(ak.NoSuchRelVarError,
             function () {
               for (var i = 0; i < 500; ++i)
                 db['r' + i]._drop();
             });
};


db_test_suite.testQuota = function () {
  check("ak.dbQuota > 0");
  db.R._create({i: number._integer(), s: string});
  var array = [];
  for (var i = 0; i < 100 * 1024; ++i)
    array.push('x');
  var str = array.join('');
  checkThrow(ak.ConstraintError,
             function () { db.R._insert({i: 0, s: str + 'x'}); });
  // Slow DB size quota test, uncomment to run
  //     checkThrow(ak.DBQuotaError,
  //                function () {
  //                    for (var i = 0; ; ++i)
  //                        db.R._insert({i: i, s: str});
  //                });
  db.R._drop();
};


db_test_suite.testDescribeApp = function () {
  checkEqualTo(items(ak.describeApp('test_app')),
               [['admin', 'test_user'],
                ['developers', ['Odysseus', 'Achilles']],
                ['email', 'a@b.com'],
                ['summary', 'test app'],
                ['description', 'test app...'],
                ['labels', ['1', '2']]]);
  checkEqualTo(items(ak.describeApp('another_app')),
               [["admin", "Odysseus"],
                ["developers", []],
                ["email", "x@y.com"],
                ["summary", "another app"],
                ["description", "another app..."],
                ["labels", ["1"]]]);
  checkThrow(ak.NoSuchAppError, "ak.describeApp('no_such_app')");
  checkThrow(ak.UsageError, "ak.describeApp()");
};


db_test_suite.testGetAdminedApps = function () {
  checkEqualTo(ak.getAdminedApps('test_user').sort(),
               ['ak', 'bad_app', 'blocking_app',
                'lib', 'test_app', 'throwing_app']);
  checkEqualTo(ak.getAdminedApps('Achilles'), []);
  checkThrow(ak.NoSuchUserError,
             function () { ak.getAdminedApps('no_such_user'); });

};


db_test_suite.testGetDevelopedApps = function () {
  checkEqualTo(ak.getDevelopedApps('Odysseus').sort(), ['ak', 'test_app']);
  checkEqualTo(ak.getDevelopedApps('test_user'), []);
  checkThrow(ak.NoSuchUserError,
             function () { ak.getDevelopedApps('no_such_user'); });
};


db_test_suite.testGetAppsByLabel = function () {
  checkEqualTo(ak.getAppsByLabel('1').sort(), ['another_app', 'test_app']);
  checkEqualTo(ak.getAppsByLabel('2'), ['test_app']);
  checkEqualTo(ak.getAppsByLabel('no_such_label'), []);
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
// Request test suite
////////////////////////////////////////////////////////////////////////////////

var request_test_suite = {};

request_test_suite.testRequest = function ()
{
  fs._write('file1', 'wuzzup');
  fs._write('file2', 'yo ho ho');
  fs._write('hello', 'hello world!');
  checkEqualTo(function () {
                 return ak._request('another_app',
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
  checkThrow(ak.NoSuchAppError, "ak._request('no_such_app', 'hi')");
  checkThrow(TypeError, "ak._request('another_app', '', {length: 1})");
  checkEqualTo(
    function () {
      fs._write('file3', 'text');
      var result = ak._request('another_app', '', [], fs._read('file3'));
      fs.remove('file3');
      return result;
    },
    '{"user":"' + ak._user + '",' +
      '"arg":"",' +
      '"data":"text",' +
      '"file_contents":[],' +
      '"issuer":"test_app"}');
  checkThrow(ak.NoSuchAppError, "ak._request('invalid/app/name', '')");
  checkThrow(ak.SelfRequestError, "ak._request('test_app', '2+2')");
  checkThrow(ak.ProcessingFailedError, "ak._request('throwing_app', '')");
  checkThrow(ak.RequestTimedOutError, "ak._request('blocking_app', '')");
  checkThrow(ak.PathError, "ak._request('another_app', '', ['..'])");
  checkThrow(ak.NoSuchEntryError,
             "ak._request('another_app', '', ['no-such-file'])");
  checkThrow(ak.EntryIsDirError,
             function () {
               fs._makeDir('dir');
               try {
                 ak._request('another_app', '', ['dir']);
               } finally {
                 fs.remove('dir');
               }
             });
  checkThrow(TypeError, "ak._request('another_app', 'hi', 42)");
};

////////////////////////////////////////////////////////////////////////////////
// Entry point
////////////////////////////////////////////////////////////////////////////////

function main() {
  runTestSuites([
                  base_test_suite,
                  db_test_suite,
                  file_test_suite,
                  request_test_suite
                ]);
  return error_count;
}


ak._main = function (expr) {
  return eval(expr);
};


'__main__.js value';
