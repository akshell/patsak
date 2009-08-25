
ak.include('ak', 'core.js');
ak.include('ak', 'mochi-kit.js');


var db = ak.db;
var rels = ak.rels;
var types = ak.types;
var constrs = ak.constrs;
var fs = ak.fs;
var apps = ak.apps;


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


function error(descr)
{
    print(prefix + descr + '\n');
    ++error_count;
}


function check(expr, descr)
{
    if (expr instanceof Function)
        check(expr(), '' + expr);
    if (!eval(expr)) {
        descr = descr || expr;
        error(descr + ' check failed');
    }
}


function checkEqualTo(expr, value)
{
    var expr_value;
    if (expr instanceof Function)
        expr_value = expr();
    else
        expr_value = eval(expr);
    if (compare(expr_value, value) != 0)
        error(expr + ' value ' + repr(expr_value) + ' != ' + repr(value));
}


function checkThrows(expr, cls)
{
    cls = cls || Error;
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


function runTestSuite(test_suite)
{
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


function runTestSuites(test_suites)
{
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


base_test_suite.testInclude = function ()
{
    check("ak.include('hello.js') == 'hello'");
    checkThrows("ak.include()");
    checkThrows("ak.include('no-such-file.js')");
    checkThrows("ak.include('bug.js')");
    checkThrows("ak.include('bug-includer.js')");
    checkThrows("ak.include('../out-of-base-dir.js')");
    checkThrows("ak.include('self-includer.js')");
    checkThrows("ak.include('cycle-includer1.js')");
    checkThrows("ak.include('no_such_lib', 'xxx.js')");
    checkEqualTo("ak.include('lib/0.1/', '/42.js')", 42);
    checkEqualTo("ak.include('lib', '0.1/42.js')", 42);
};


base_test_suite.testTypes = function ()
{
    check("types.a === undefined");
    check("'number' in types && 'string' in types && 'boolean' in types");
    check("!('abc' in types)");

    var fields = [];
    for (var field in types)
        fields.push(field);
    checkEqualTo(fields, 'number,string,boolean');

    check("types.number.name == 'number'");
    check("types.string.name == 'string'");
    check("types.boolean.name == 'boolean'");
};


base_test_suite.testConstructors = function ()
{
    check("this instanceof ak.Global");
    check("ak instanceof ak.AK");
    check("db instanceof ak.DB");
    check("types instanceof ak.Types");
    check("rels instanceof ak.Rels");
};


base_test_suite.testSetObjectProp = function ()
{
    var obj = {};
    checkThrows("ak._setObjectProp()");
    checkThrows("ak._setObjectProp(1, 'f', 0, 42)");
    ak._setObjectProp(obj, 'read_only', ak.READ_ONLY, 1);
    obj.setProp('dont_enum', ak.DONT_ENUM, 2);
    obj.setProp('dont_delete', ak.DONT_DELETE, 3);
    checkThrows(function () { ak._setObjectProp(obj, 'field', {}, 42); });
    checkThrows(function () { ak._setObjectProp(obj, 'field', 8, 42); });
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


base_test_suite.testAppName = function ()
{
    check("ak._appName == 'test_app'");
};


base_test_suite.testReadCode = function ()
{
    check("ak._readCode('subdir/hi.txt') == 'russian привет\\n'");
    check("ak._readCode('bad_app', 'main.js') == 'wuzzup!!!!!!!!\\n'");
    checkThrows("ak._readCode('illegal/name', 'main.js')");
    checkThrows("ak._readCode('test_app', '')");
    checkThrows("ak._readCode('test_app', 'subdir/../../ak/main.js')");
    checkThrows("ak._readCode()");
    check(function () {
              return (ak._readCode('subdir/hi.txt') ==
                      ak.readCode('test_app/subdir', 'hi.txt'));
          });
};


base_test_suite.testCompile = function ()
{
    check(function () {
              var script = ak._compile('2+2');
              return script._run() === 4;
          });
    checkThrows("ak._compile()");
    check(function () {
              try {
                  ak._compile('(');
              } catch (error) {
                  return error instanceof SyntaxError;
              }
              return false;
          });
    check(function () {
              try {
                  ak._compile('asdfjkl')._run();
              } catch (error) {
                  return error instanceof ReferenceError;
              }
              return false;
          });
    check(function () {
              try {
                  ak._compile('ak._compile("(")', 'just string')._run();
              } catch (error) {
                  return error instanceof SyntaxError;
              }
              return false;
          });
};

////////////////////////////////////////////////////////////////////////////////
// DB test suite
////////////////////////////////////////////////////////////////////////////////

var db_test_suite = {};


var number = ak.types.number;
var string = ak.types.string;
var boolean = ak.types.boolean;
var date = ak.types.date;


db_test_suite.setUp = function ()
{
    db._dropRels(keys(rels));

    db._createRel('Dummy',
                  {id: number});
    db._createRel('Empty', {});
    db._createRel('User',
                  {id: number._serial()._unique(),
                   name: string._unique(),
                   age: number,
                   flooder: boolean._default('yo!')});
    db._createRel('Post',
                  {id: number._serial()._unique(),
                   title: string,
                   text: string,
                   author: number._int()._foreign('User', 'id')},
                  constrs._unique(['title', 'author']));
    db._createRel('Comment',
                  {id: number._serial()._unique(),
                   text: string,
                   author: number._int()._foreign('User', 'id'),
                   post: number._int()._foreign('Post', 'id')});

    rels.User._insert({name: 'anton', age: 22, flooder: true});
    rels.User._insert({name: 'marina', age: 25, flooder: false});
    rels.User._insert({name: 'den', age: 23});

    rels.Post._insert({title: 'first', text: 'hello world', author: 0});
    rels.Post._insert({title: 'second', text: 'hi', author: 1});
    rels.Post._insert({title: 'third', text: 'yo!', author: 0});

    rels.Comment._insert({text: ':-*', author: 1, post: 0});
    rels.Comment._insert({text: 'rrr', author: 0, post: 0});
    rels.Comment._insert({text: 'ololo', author: 2, post: 2});
};


db_test_suite.tearDown = function ()
{
    db._dropRels(keys(rels));
};


db_test_suite.testConstructors = function ()
{
    var q = db._query('User');
    check(q instanceof ak.Query, "ak.Query");
    check(q[0] instanceof ak.Tuple, "ak.Tuple");
};


db_test_suite.testCreateRel = function ()
{
    checkThrows("db._createRel('illegal')");
    checkThrows("db._createRel('illegal', 'str')");
    checkThrows("db._createRel('illegal', {'field': 15})");
    checkThrows("db._createRel('@', {})");
    checkThrows("db._createRel('1a', {})");
    checkThrows("db._createRel('ab#cd', {})");
    checkThrows("db._createRel('', {})");
    checkThrows("db._createRel('User', {})");
    checkThrows(function () {
                    var obj = {length: 15};
                    db._createRel('illegal', {x: number}, constrs._unique(obj));
                });

    var obj = {toString: function () { return 'x'; }};
    db._createRel('legal', {x: number}, constrs._unique(obj));
    rels.legal._drop();

    obj.length = 12.1;
    db._createRel('legal', {x: number}, constrs._unique(obj));
    rels.legal._drop();

    obj.length = -1;
    db._createRel('legal', {x: number}, constrs._unique(obj));
    rels.legal._drop();

    checkThrows(function () {
                    db._createRel('illegal', {x: number}, constrs._unique([]));
                });

    checkThrows(function () {
                    db._createRel('illegal',
                                  {x: number, y: number},
                                  constrs._foreign(['x', 'y'], 'User', 'id'));
                });

    checkThrows(function () {
                    var obj = {length: 1};
                    db._createRel('illegal',
                                  {undefined: number},
                                  constrs._foreign(obj, 'Post', 'id'));
                });
    checkThrows(function () {
                    db._createRel('illegal',
                                  {x: number, y: number},
                                  constrs._foreign(['x', 'y'],
                                                   'Post',
                                                   ['id', 'author']));
                });
    checkThrows(function () {
                    var name = '';
                    for (var i = 0; i < 61; ++i)
                        name += 'x';
                    db._createRel(name, {});
                });
    checkThrows(function () {
                    attrs = {};
                    for (var i = 0; i < 1000; ++i)
                        attrs['attr' + i] = number;
                    db._createRel('illegal', attrs);
                });
};


db_test_suite.testConstr = function ()
{
    checkThrows("db._createRel('illegal', {}, constrs._unique())");
    checkThrows("constrs._foreign('a', 'b')");
    checkThrows("constrs._check('a', 'b')");
    checkThrows("constrs._unique(['a', 'a'])");
    checkThrows("constrs._unique('a', 'a')");
    constrs._check('field != 0');
    checkThrows("db._createRel('ill', {x: number}, {})");
    checkThrows(function () {
                    db._createRel('ill',
                                  {x: number},
                                  constrs._foreign([], 'User', []));
                });
    checkThrows(function () {
                    db._createRel('ill',
                                  {x: number},
                                  constrs._foreign('x', 'User', 'age'));
                });
};


db_test_suite.testDropRels = function ()
{
    db._createRel('NewRel', {x: number});
    rels.NewRel._drop();
    check("!('NewRel' in rels)");
    checkThrows("rels.User._drop(1)");
    checkThrows("rels.User._drop()");
    checkThrows("db._dropRels('User', 'Post')");
    checkThrows("db._dropRels('Comment', 'Comment')");
    checkThrows("db._dropRels(['Comment', 'Comment'])");

    db._createRel('rel1', {x: number}, constrs._unique('x'));
    db._createRel('rel2', {x: number}, constrs._foreign('x', 'rel1', 'x'));
    checkThrows("rels.rel1._drop()");
    db._dropRels('rel1', 'rel2');
};


db_test_suite.testQuery = function ()
{
    checkThrows(function () { db._query(); });
    var q = db._query('User[name, age, flooder] where id == 0');
    checkEqualTo(q, [{name: 'anton', age: 22, flooder: true}]);
    check(function () { return q[1] === undefined; });
    check(function () { return (0 in q) && !(1 in q); });
    checkEqualTo(function () { return keys(q); }, [0]);
    checkThrows("db._query()");
    checkThrows("db._query('dfsa')._perform()");
    checkThrows("db._query('dfsa').length");
    checkThrows("db._query('dfsa')[0]");
    check("!(0 in db._query('dfsa'))");
    check("compare(keys(db._query('dfsa')), []) == 0");
    checkThrows("db._query('User')._perform(1)");
    check("db._query('User')._perform() === undefined");
    checkEqualTo(function () {
                     return db._query('Post.author->name where id == $', 0);
                 },
                 [{name: 'anton'}]);
};


db_test_suite.testInsert = function ()
{
    checkThrows("rels.User._insert()");
    checkThrows("rels.User._insert(15)", TypeError);
    checkThrows("rels.User._insert({'@': 'abc'})");
    checkThrows("rels.Comment._insert({id: 2, text: 'yo', author: 5, post: 0})");
    checkThrows("rels.User._insert({id: 2})");
    checkThrows("rels.Empty._insert({x: 5})");
    checkEqualTo("items(rels.User._insert({name: 'xxx', age: 0}))",
                [['id', 3], ['name', 'xxx'], ['age', 0], ['flooder', true]]);
    rels.User._where('name == $', 'xxx')._del();
    checkEqualTo("items(rels.Empty._insert({}))", []);
    checkThrows("rels.Empty._insert({})");
    rels.Empty._all()._del();
};


db_test_suite.testRel = function ()
{
    check("rels.User.name == 'User'");
    checkEqualTo("items(rels.User.header).sort()",
                 [['age', number],
                  ['flooder', boolean],
                  ['id', number],
                  ['name', string]]);
    check("'name' in rels.User");
    check("'header' in rels.User");
    check("'_insert' in rels.User");
};


db_test_suite.testRels = function ()
{
    check("'Comment' in rels");
    check("!('second' in rels)");
    check("rels.second === undefined");
    checkEqualTo("keys(rels).sort()",
                 ['Comment', 'Dummy', 'Empty', 'Post', 'User']);
};


db_test_suite.testWhere = function ()
{
    checkEqualTo(function () {
                     return (db._query('User[id, name]')
                             ._where('id == $1 && name == $2', 0, 'anton'));
                 },
                 [{id: 0, name: 'anton'}]);
    checkThrows("db._query('User')._where()");
    checkEqualTo("rels.User._where('forsome (x in {}) true').length", 3);
};


db_test_suite.testWhose = function ()
{
    checkEqualTo("db._query('User[id, name]').whose('id == $', 1)",
                 {id: 1, name: 'marina'});
    checkThrows("db._query('User').whose(true)");
};


db_test_suite.testBy = function ()
{
    db._createRel('ByTest', {x: number, y: number});
    rels.ByTest._insert({x: 0, y: 1});
    rels.ByTest._insert({x: 1, y: 7});
    rels.ByTest._insert({x: 2, y: 3});
    rels.ByTest._insert({x: 3, y: 9});
    rels.ByTest._insert({x: 4, y: 4});
    rels.ByTest._insert({x: 5, y: 2});
    check("db._query('ByTest')._where('y != $', 9)" +
          "._by('x * $1 % $2', 2, 7).field('y')",
          [1, 4, 7, 2, 3]);
    rels.ByTest._drop();
};


db_test_suite.testOnly = function ()
{
    checkThrows(function () {
                    var obj = {length: 1};
                    db._query('User')._only(obj);
                });
};


db_test_suite.testAll = function ()
{
    checkThrows("rels.User._all(1)");
    check("rels.User._all().field('id').sort()", [0, 1, 2]);
    check("rels.User._all()._where('!(id % 2)')._by('-id').field('name')",
          ['den', 'anton']);
    check("rels.User.whose('id == $', 0)['name']",
          'anton');
    check("rels.User._where('name != $', 'den')._by('flooder').field('id')",
          [1, 0]);
};


db_test_suite.testUpdate = function ()
{
    var initial = rels.User._all();
    initial._perform();
    checkThrows("rels.User._where('id == 0')._update({})");
    checkThrows("rels.User._where('id == 0')._update()");
    checkThrows("rels.User._where('id == 0')._update(1)");
    checkEqualTo("rels.User._where('id == 0')._update({name: '$'}, 'ANTON')",
                 1);
    check("rels.User.whose('id == 0')['name'] == 'ANTON'");
    checkThrows("rels.User._all()._update({}");
    var rows_number = rels.User
                          ._where('name != $', 'marina')
                          ._by('id')._update({age: 'age + $1',
                                              flooder: 'flooder || $2'},
                                              2,
                                              'yo!');
    check(rows_number == 2);
    for (var i = 0; i < 10; ++ i)
        checkThrows(
            "rels.User._where('name == $', 'den').updateByValues({id: 4})");
    forEach(initial, function (tuple) {
                rels.User._where('id == $', tuple.id).updateByValues(tuple);
            });
    checkEqualTo("rels.User._all()", initial);
};


db_test_suite.testDelete = function ()
{
    var initial = rels.User._all();
    initial._perform();
    checkThrows("rels.User._all()._del()");
    var tricky_name = 'xx\'y\'zz\'';
    rels.User._insert({id: 3, name: tricky_name, age: 15, flooder: true});
    checkThrows("rels.User._where('age == 15')._del(0)");
    rels.User._by('name')._where('id == 3')._update({name: 'name + 1'});
    checkEqualTo("rels.User.whose('id == 3')['name']", tricky_name + 1);
    checkEqualTo("rels.User._where('age == 15')._del()", 1);
    checkEqualTo("rels.User.field('id').sort()", [0, 1, 2]);
};


db_test_suite.testStress = function ()
{
    for (var i = 0; i < 20; ++i) {
        this.testUpdate();
        this.testDelete();
        this.testQuery();
    };
};


db_test_suite.testPg = function ()
{
    db._createRel('pg_class', {x: number});
    rels.pg_class._insert({x: 0});
    checkEqualTo("rels.pg_class._all()", [{x: 0}]);
    rels.pg_class._drop();
};


db_test_suite.testCheck = function ()
{
    db._createRel('silly', {n: number._check('n != 42')});
    db._createRel('dummy', {b: boolean, s: string},
                  constrs._check('b || s == "hello"'));
    rels.silly._insert({n: 0});
    checkThrows("rels.r._insert({n: 42})");
    rels.dummy._insert({b: true, s: 'hi'});
    rels.dummy._insert({b: false, s: 'hello'});
    checkThrows("rels.dummy._insert({b: false, s: 'oops'})");
    rels.silly._drop();
    rels.dummy._drop();
};


db_test_suite.testDate = function ()
{
    db._createRel('d1', {d: date}, constrs._unique('d'));
    var some_date = new Date(Date.parse('Wed, Mar 04 2009 16:12:09 GMT'));
    var other_date = new Date(2009, 0, 15, 13, 27, 11, 481);
    rels.d1._insert({d: some_date});
    checkEqualTo("rels.d1.field('d')", [some_date]);
    db._createRel('d2', {d: date}, constrs._foreign('d', 'd1', 'd'));
    checkThrows(function () { rels.d2._insert({d: other_date}); });
    rels.d1._insert({d: other_date});
    checkEqualTo("rels.d1._by('-d').field('d')", [some_date, other_date]);
    rels.d2._insert({d: other_date});
    db._dropRels('d1', 'd2');
};


db_test_suite.testDefault = function ()
{
    var now = new Date();
    db._createRel('def', {n: number._default(42),
                          s: string._default('hello, world!'),
                          b: boolean._default(true),
                          d: date._default(now)});
    checkEqualTo("items(rels.def._getDefaults()).sort()",
                 [['b', true],
                  ['d', now],
                  ['n', 42],
                  ['s', 'hello, world!']]);
    rels.def._insert({});
    checkThrows("rels.def._insert({})");
    rels.def._insert({b: false});
    rels.def._insert({n: 0, s: 'hi'});
    checkEqualTo("rels.def._by('b')._by('n')",
                 [{b: false, d: now, n: 42, s: 'hello, world!'},
                  {b: true, d: now, n: 0, s: 'hi'},
                  {b: true, d: now, n: 42, s: 'hello, world!'}]);
    rels.def._drop();
};


db_test_suite.testIntSerial = function ()
{
    checkThrows("number._serial()._default(42)");
    checkThrows("number._default(42)._serial()");
    checkThrows("number._serial()._int()");
    checkThrows("number._serial()._serial()");
    checkThrows("number._int()._int()");
    checkEqualTo("rels.Comment._getInts().sort()", ['author', 'id', 'post']);
    db._createRel('r', {x: number._serial(),
                        y: number._serial(),
                        z: number._int()});
    checkEqualTo("rels.r._getInts().sort()", ['x', 'y', 'z']);
    checkEqualTo("rels.r._getSerials().sort()", ['x', 'y']);
    rels.r._drop();
};


db_test_suite.test_Unique = function ()
{
    db._createRel('r',
                  {a: number, b: string, c: boolean},
                  constrs._unique('a', 'b'),
                  constrs._unique('b', 'c'),
                  constrs._unique('c'));
    checkEqualTo("rels.r._getUniques().sort()",
                 [['a', 'b'], ['a', 'b', 'c'], ['b', 'c'], ['c']]);
    checkEqualTo("rels.Dummy._getUniques()", [['id']]);
    rels.r._drop();
};


db_test_suite.testForeignKey = function ()
{
    db._createRel('r',
                  {title: string,
                   author: number._int(),
                   id: number._serial(),
                   ref: number._int()},
                  constrs._foreign(['title', 'author'],
                                   'Post',
                                   ['title', 'author']),
                  constrs._foreign('ref', 'r', 'id'),
                  constrs._unique('id'));
    checkEqualTo(function () {
                     return map(function (fk) {
                                    return items(fk).sort();
                                },
                                rels.r._getForeigns()).sort();
                 },
                 [[["keyFields", ["ref"]],
                   ["refFields", ["id"]],
                   ["refRel", "r"]],
                  [["keyFields", ["title", "author"]],
                   ["refFields", ["title", "author"]],
                   ["refRel", "Post"]]]);
    rels.r._drop();
};


db_test_suite.testRelNumber = function ()
{
    checkThrows(function () {
                    for (var i = 0; i < 500; ++i)
                        db._createRel('r' + i, {});
                });
    checkThrows(function () {
                    for (var i = 0; i < 500; ++i)
                        rels['r' + i]._drop();
                });
};


db_test_suite.testStringLength = function ()
{
    db._createRel('Str', {s: string});
    var array = [];
    for (var i = 0; i < 100*1024; ++i)
        array.push('x');
    checkThrows(function () { rels.Str._insert({s: array.join('')}); });
    rels.Str._drop();
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
};


file_test_suite.tearDown = function ()
{
    forEach(ak.fs._list(''), ak.fs.remove);
};


file_test_suite.testRead = function ()
{
    checkEqualTo("fs._read('//dir1////subdir/hello')",
                 'hello world!');
    checkThrows("fs._read('does_not_exists')");
    checkThrows("fs._read('dir1')");
    checkThrows("fs._read('//..//test_app/dir1/subdir/hello')");
    checkThrows("fs._read('/dir1/../../file')");
    checkThrows("fs._read('////')");
    checkThrows(function () {
                    var path = '';
                    for (var i = 0; i < 40; ++i)
                        path += '/dir';
                    fs._read(path);
                });
};


file_test_suite.testList = function ()
{
    checkEqualTo("fs._list('').sort()", ['dir1', 'dir2', 'file']);
};


file_test_suite.testExists = function ()
{
    checkEqualTo("fs._exists('')", true);
    checkEqualTo("fs._exists('dir1/subdir/hello')", true);
    checkEqualTo("fs._exists('no/such')", false);
};


file_test_suite.testIsDir = function ()
{
    checkEqualTo("fs._isDir('')", true);
    checkEqualTo("fs._isDir('dir2')", true);
    checkEqualTo("fs._isDir('file')", false);
    checkEqualTo("fs._isDir('no/such')", false);
};


file_test_suite.testIsFile = function ()
{
    checkEqualTo("fs._isFile('')", false);
    checkEqualTo("fs._isFile('dir1/subdir/hello')", true);
    checkEqualTo("fs._isFile('dir1/subdir')", false);
    checkEqualTo("fs._isFile('no/such')", false);
};


file_test_suite.testRm = function ()
{
    fs._write('new-file', 'data');
    fs._remove('new-file');
    fs._makeDir('dir2/new-dir');
    fs._remove('dir2/new-dir');
    checkEqualTo("fs._list('').sort()", ['dir1', 'dir2', 'file']);
    checkEqualTo("fs._list('dir2')", []);
};


file_test_suite.testWrite = function ()
{
    fs._write('wuzzup', 'yo wuzzup!');
    checkEqualTo("fs._read('wuzzup')", 'yo wuzzup!');
    fs._write('hello', fs._read('dir1/subdir/hello'));
    checkEqualTo("fs._read('hello')", 'hello world!');
    fs._remove('wuzzup');
};


file_test_suite.testMkDir = function ()
{
    fs._makeDir('dir2/ddd');
    checkEqualTo("fs._list('dir2')", ['ddd']);
    checkEqualTo("fs._list('dir2/ddd')", []);
    fs._remove('dir2/ddd');
};


file_test_suite.testCopyFile = function ()
{
    fs._copyFile('dir1/subdir/hello', 'dir2/hello');
    checkEqualTo("fs._read('dir2/hello')", 'hello world!');
    fs._remove('dir2/hello');
    checkThrows("fs._copyFile('no_such', 'never_created')");
    checkThrows("fs._copyFile('file', 'dir1/subdir')");
};


file_test_suite.testRename = function ()
{
    fs._rename('dir1', 'dir2/dir3');
    checkEqualTo("fs._read('dir2/dir3/subdir/hello')", 'hello world!');
    fs._rename('dir2/dir3', 'dir1');
};


file_test_suite.testQuota = function ()
{
    var array = [];
    for (var i = 0; i < 1024 * 1024; ++i)
        array.push('x');
    var str = array.join('');
    checkThrows(function () {
                    for (var i = 0; i < 11; ++i)
                        fs._write('file' + i, str);
                });
    for (i = 0; i < 10; ++i)
        fs._remove('file' + i);
    check("!fs._exists('file10')");
    checkThrows(function () {
                    var big_str = [str, str, str, str, str].join('');
                    fs._write('big_file', big_str);
                });
};

////////////////////////////////////////////////////////////////////////////////
// File test suite
////////////////////////////////////////////////////////////////////////////////

var call_test_suite = {};


call_test_suite.testCall = function ()
{
    fs._write('file1', 'wuzzup');
    fs._write('file2', 'yo ho ho');
    checkEqualTo(function () {
                     return apps.another_app._call('hello world!',
                                                   ['file1', 'file2'],
                                                   'yo!!!');
                 },
                 '{"user":"' + ak._user + '",'+
                 '"arg":"hello world!","data":"yo!!!",' +
                 '"file_contents":["wuzzup","yo ho ho"],' +
                 '"requester_app":"test_app"}');
    check("!fs._exists('file1') && !fs._exists('file2')");
    checkThrows("apps.no_such_app._call('hi')");
    check("'another_app' in apps");
    check("!('no_such_app' in apps)");
    checkThrows("apps.another_app._call('', {length: 1})");
    checkEqualTo(
        function () {
            fs._write('file3', 'text');
            var result = apps.another_app._call('', [], fs._read('file3'));
            fs.remove('file3');
            return result;
        },
        '{"user":"' + ak._user + '",' +
        '"arg":"",' +
        '"data":"text",' +
        '"file_contents":[],' +
        '"requester_app":"test_app"}');
    checkThrows("apps['invalid/app/name']._call('')");
    checkThrows("apps.test_app._call('2+2')");
    checkThrows("apps.throwing_app._call('')");
    checkThrows("apps.blocking_app._call('')");
    checkThrows("apps.another_app._call('', ['..'])");
    checkThrows("apps.another_app._call('', ['no-such-file'])");
    checkThrows(function () {
                    fs._makeDir('dir');
                    try {
                        apps.another_app._call('', ['dir']);
                    } finally {
                        fs.remove('dir');
                    }
                });
};

////////////////////////////////////////////////////////////////////////////////
// Entry point
////////////////////////////////////////////////////////////////////////////////

function main()
{
    runTestSuites([base_test_suite,
                   db_test_suite,
                   file_test_suite,
                   call_test_suite]);
    return error_count;
}


function bug()
{
    throw 42;
}


function fileTest()
{

}


ak._main = function (expr)
{
    return eval(expr);
};
