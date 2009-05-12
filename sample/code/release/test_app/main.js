
import('ak', 'core.js');
import('ak', 'mochi-kit.js');


var db = ak.db;
var rel = ak.rel;
var type = ak.type;
var constr = ak.constr;
var fs = ak.fs;
var apps = ak.apps;


function print(x) {
    ak._print(x);
}

function println() {
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
        return check(expr(), '' + expr);
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
            expr()
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


base_test_suite.testInclude = function()
{
    check("include('hello.js') == 'hello'");
    checkThrows("include('no-such-file.js')");
    checkThrows("include('hello.js', 'hello.js')");
    checkThrows("include('bug.js')");
    checkThrows("include('bug-includer.js')");
    checkThrows("include('../out-of-base-dir.js')");
    checkThrows("include('unreadable.js')");
    check("include('subdir/another-hello.js') == 'hello'");
    checkThrows("include('self-includer.js')");
};


base_test_suite.testImport = function()
{
    checkThrows("import('no_such_lib', 'xxx.js')");
    checkEqualTo("import('lib/0.1/', '42.js')", 42);
};


base_test_suite.testType = function()
{
    check("type.a === undefined");
    check("'number' in type && 'string' in type && 'boolean' in type");
    check("!('abc' in type)");

    var fields = [];
    for (var field in type)
        fields.push(field);
    check(fields == 'number,string,boolean', 'types list');

    check("type.number.name == 'number'");
    check("type.string.name == 'string'");
    check("type.boolean.name == 'boolean'");
};


base_test_suite.testConstructors = function()
{
    check("this instanceof ak.Global");
    check("ak instanceof ak.AK");
    check("db instanceof ak.DB");
    check("type instanceof ak.TypeCatalog");
    check("rel instanceof ak.RelCatalog");
};


base_test_suite.testSetObjectProp = function()
{
    var obj = {};
    checkThrows("ak.setObjectProp()");
    checkThrows("ak.setObjectProp(1, 'f', 0, 42)");
    ak.setObjectProp(obj, 'read_only', ak.READ_ONLY, 1);
    obj.setProp('dont_enum', ak.DONT_ENUM, 2);
    obj.setProp('dont_delete', ak.DONT_DELETE, 3);
    checkThrows(function () { ak.setObjectProp(obj, 'field', {}, 42); });
    checkThrows(function () { ak.setObjectProp(obj, 'field', 8, 42); });
    check(function() {
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

////////////////////////////////////////////////////////////////////////////////
// DB test suite
////////////////////////////////////////////////////////////////////////////////

var db_test_suite = {};


var number = ak.type.number;
var string = ak.type.string;
var boolean = ak.type.boolean;
var date = ak.type.date;


db_test_suite.setUp = function()
{
    db.dropRels(keys(rel));

    db.createRel('Dummy',
                 {id: number});
    db.createRel('Empty', {});
    db.createRel('User',
                 {id: number.serial().unique(),
                  name: string.unique(),
                  age: number,
                  flooder: boolean.byDefault('yo!')});
    db.createRel('Post',
                 {id: number.serial().unique(),
                  title: string,
                  text: string,
                  author: number.int().foreignKey('User', 'id')},
                 constr.unique(['title', 'author']));
    db.createRel('Comment',
                 {id: number.serial().unique(),
                  text: string,
                  author: number.int().foreignKey('User', 'id'),
                  post: number.int().foreignKey('Post', 'id')});

    rel.User.insert({name: 'anton', age: 22, flooder: true});
    rel.User.insert({name: 'marina', age: 25, flooder: false});
    rel.User.insert({name: 'den', age: 23});

    rel.Post.insert({title: 'first', text: 'hello world', author: 0});
    rel.Post.insert({title: 'second', text: 'hi', author: 1});
    rel.Post.insert({title: 'third', text: 'yo!', author: 0});

    rel.Comment.insert({text: ':-*', author: 1, post: 0});
    rel.Comment.insert({text: 'rrr', author: 0, post: 0});
    rel.Comment.insert({text: 'ololo', author: 2, post: 2});
};


db_test_suite.tearDown = function()
{
    db.dropRels(keys(rel));
};


db_test_suite.testConstructors = function()
{
    var q = db.query('User');
    check(q instanceof ak.Query, "ak.Query");
    check(q[0] instanceof ak.Tuple, "ak.Tuple");
};


db_test_suite.testCreateRel = function()
{
    checkThrows("db.createRel('illegal')");
    checkThrows("db.createRel('illegal', 'str')");
    checkThrows("db.createRel('illegal', {'field': 15})");
    checkThrows("db.createRel('@', {})");
    checkThrows("db.createRel('1a', {})");
    checkThrows("db.createRel('ab#cd', {})");
    checkThrows("db.createRel('', {})");
    checkThrows("db.createRel('User', {})");
    checkThrows(function () {
                    var obj = {length: 15};
                    db.createRel('illegal', {x: number}, constr.unique(obj));
                });

    var obj = {toString: function () { return 'x'; }};
    db.createRel('legal', {x: number}, constr.unique(obj));
    rel.legal.drop();

    obj.length = 12.1;
    db.createRel('legal', {x: number}, constr.unique(obj));
    rel.legal.drop();

    obj.length = -1;
    db.createRel('legal', {x: number}, constr.unique(obj));
    rel.legal.drop();

    checkThrows(function () {
                    db.createRel('illegal', {x: number}, constr.unique([]));
                });

    checkThrows(function () {
                    db.createRel('illegal',
                                 {x: number, y: number},
                                 constr.foreignKey(['x', 'y'], 'User', 'id'));
                });

    checkThrows(function () {
                    var obj = {length: 1};
                    db.createRel('illegal',
                                 {undefined: number},
                                 constr.foreignKey(obj, 'Post', 'id'));
                });
    checkThrows(function () {
                    db.createRel('illegal',
                                 {x: number, y: number},
                                 constr.foreignKey(['x', 'y'],
                                                   'Post',
                                                   ['id', 'author']));
                });
    checkThrows(function () {
                    var name = '';
                    for (var i = 0; i < 61; ++i)
                        name += 'x';
                    db.createRel(name, {});
                });
    checkThrows(function () {
                    attrs = {};
                    for (var i = 0; i < 1000; ++i)
                        attrs['attr' + i] = number;
                    db.createRel('illegal', attrs);
                });
};


db_test_suite.testConstr = function()
{
    checkThrows("db.createRel('illegal', {}, constr.unique())");
    checkThrows("constr.foreignKey('a', 'b')");
    checkThrows("constr.check('a', 'b')");
    checkThrows("constr.unique(['a', 'a'])");
    checkThrows("constr.unique('a', 'a')");
    constr.check('field != 0');
    checkThrows("db.createRel('ill', {x: number}, {})");
    checkThrows(function () {
                    db.createRel('ill',
                                 {x: number},
                                 constr.foreignKey([], 'User', []));
                });
    checkThrows(function () {
                    db.createRel('ill',
                                 {x: number},
                                 constr.foreignKey('x', 'User', 'age'));
                });
};


db_test_suite.testDropRels = function()
{
    db.createRel('NewRel', {x: number});
    rel.NewRel.drop();
    check("!('NewRel' in rel)");
    checkThrows("rel.User.drop(1)");
    checkThrows("rel.User.drop()");
    checkThrows("db.dropRels('User', 'Post')");
    checkThrows("db.dropRels('Comment', 'Comment')");
    checkThrows("db.dropRels(['Comment', 'Comment'])");

    db.createRel('rel1', {x: number}, constr.unique('x'));
    db.createRel('rel2', {x: number}, constr.foreignKey('x', 'rel1', 'x'));
    checkThrows("rel.rel1.drop()");
    db.dropRels('rel1', 'rel2');
};


db_test_suite.testQuery = function()
{
    var q = db.query('User[name, age, flooder] where id == 0');
    checkEqualTo(q, [{name: 'anton', age: 22, flooder: true}]);
    check(function() { return q[1] === undefined; });
    check(function() { return (0 in q) && !(1 in q); });
    checkEqualTo(function() { return keys(q); }, [0]);
    checkThrows("db.query()");
    checkThrows("db.query('dfsa').perform()");
    checkThrows("db.query('dfsa').length");
    checkThrows("db.query('dfsa')[0]");
    check("!(0 in db.query('dfsa'))");
    check("compare(keys(db.query('dfsa')), []) == 0");
    checkThrows("db.query('User').perform(1)");
    check("db.query('User').perform() === undefined");
    checkEqualTo(function () {
                     return db.query('Post.author->name where id == $', 0);
                 },
                 [{name: 'anton'}]);
};


db_test_suite.testInsert = function()
{
    checkThrows("rel.User.insert()");
    checkThrows("rel.User.insert(15)", TypeError);
    checkThrows("rel.User.insert({'@': 'abc'})");
    checkThrows("rel.Comment.insert({id: 2, text: 'yo', author: 5, post: 0})");
    checkThrows("rel.User.insert({id: 2})");
    checkThrows("rel.Empty.insert({x: 5})");
    checkEqualTo("items(rel.User.insert({name: 'xxx', age: 0}))",
                [['id', 3], ['name', 'xxx'], ['age', 0], ['flooder', true]]);
    rel.User.where('name == $', 'xxx').del();
    checkEqualTo("items(rel.Empty.insert({}))", []);
    checkThrows("rel.Empty.insert({})");
    rel.Empty.all().del();
};


db_test_suite.testRel = function()
{
    check("rel.User.name == 'User'");
    checkEqualTo("items(rel.User.header).sort()",
                 [['age', number],
                  ['flooder', boolean],
                  ['id', number],
                  ['name', string]]);
    check("'name' in rel.User");
    check("'header' in rel.User");
    check("'insert' in rel.User");
};


db_test_suite.testRelCatalog = function()
{
    check("'Comment' in rel");
    check("!('second' in rel)");
    check("rel.second === undefined");
    checkEqualTo("keys(rel).sort()",
                 ['Comment', 'Dummy', 'Empty', 'Post', 'User']);
};


db_test_suite.testWhere = function()
{
    checkEqualTo(function () {
                     return (db.query('User[id, name]')
                             .where('id == $1 && name == $2', 0, 'anton'));
                 },
                 [{id: 0, name: 'anton'}]);
    checkThrows("db.query('User').where()");
    checkEqualTo("rel.User.where('forsome (x in {}) true').length", 3);
};


db_test_suite.testWhose = function()
{
    checkEqualTo("db.query('User[id, name]').whose('id == $', 1)",
                 {id: 1, name: 'marina'});
    checkThrows("db.query('User').whose(true)");
};


db_test_suite.testBy = function()
{
    db.createRel('ByTest', {x: number, y: number});
    rel.ByTest.insert({x: 0, y: 1});
    rel.ByTest.insert({x: 1, y: 7});
    rel.ByTest.insert({x: 2, y: 3});
    rel.ByTest.insert({x: 3, y: 9});
    rel.ByTest.insert({x: 4, y: 4});
    rel.ByTest.insert({x: 5, y: 2});
    check("db.query('ByTest').where('y != $', 9)" +
          ".by('x * $1 % $2', 2, 7).field('y')",
          [1, 4, 7, 2, 3]);
    rel.ByTest.drop();
};


db_test_suite.testOnly = function()
{
    checkThrows(function () {
                    var obj = {length: 1};
                    db.query('User').only(obj);
                });
};


db_test_suite.testAll = function()
{
    checkThrows("rel.User.all(1)");
    check("rel.User.all().field('id').sort()", [0, 1, 2]);
    check("rel.User.all().where('!(id % 2)').by('-id').field('name')",
          ['den', 'anton']);
    check("rel.User.whose('id == $', 0)['name']",
          'anton');
    check("rel.User.where('name != $', 'den').by('flooder').field('id')",
          [1, 0]);
};


db_test_suite.testUpdate = function()
{
    var initial = rel.User.all();
    initial.perform();
    checkThrows("rel.User.where('id == 0').update({})");
    checkThrows("rel.User.where('id == 0').update()");
    checkThrows("rel.User.where('id == 0').update(1)");
    rel.User.where('id == 0').update({name: '$'}, 'ANTON');
    check("rel.User.whose('id == 0')['name'] == 'ANTON'");
    checkThrows("rel.User.all().update({}");
    rel.User
        .where('name != $', 'marina')
        .by('id').update({age: 'age + $1',
                          flooder: 'flooder || $2'},
                         2,
                         'yo!');
    for (var i = 0; i < 10; ++ i)
        checkThrows(
            "rel.User.where('name == $', 'den').updateByValues({id: 4})");
    forEach(initial, function (tuple) {
                rel.User.where('id == $', tuple.id).updateByValues(tuple);
            });
    checkEqualTo("rel.User.all()", initial);
};


db_test_suite.testDel = function()
{
    var initial = rel.User.all();
    initial.perform();
    checkThrows("rel.User.all().del()");
    var tricky_name = 'xx\'y\'zz\'';
    rel.User.insert({id: 3, name: tricky_name, age: 15, flooder: true});
    checkThrows("rel.User.where('age == 15').del(0)");
    rel.User.by('name').where('id == 3').update({name: 'name + 1'});
    checkEqualTo("rel.User.whose('id == 3')['name']", tricky_name + 1);
    rel.User.where('age == 15').del();
    checkEqualTo("rel.User.field('id').sort()", [0, 1, 2]);
};


db_test_suite.testStress = function()
{
    for (var i = 0; i < 20; ++i) {
        this.testUpdate();
        this.testDel();
        this.testQuery();
    };
};


db_test_suite.testPg = function()
{
    db.createRel('pg_class', {x: number});
    rel.pg_class.insert({x: 0});
    checkEqualTo("rel.pg_class.all()", [{x: 0}]);
    rel.pg_class.drop();
};


db_test_suite.testCheck = function()
{
    db.createRel('silly', {n: number.check('n != 42')});
    db.createRel('dummy', {b: boolean, s: string},
                 constr.check('b || s == "hello"'));
    rel.silly.insert({n: 0});
    checkThrows("rel.r.insert({n: 42})");
    rel.dummy.insert({b: true, s: 'hi'});
    rel.dummy.insert({b: false, s: 'hello'});
    checkThrows("rel.dummy.insert({b: false, s: 'oops'})");
    rel.silly.drop();
    rel.dummy.drop();
};


db_test_suite.testDate = function()
{
    db.createRel('d1', {d: date}, constr.unique('d'));
    var some_date = new Date(Date.parse('Wed, Mar 04 2009 16:12:09 GMT'));
    var other_date = new Date(2009, 0, 15, 13, 27, 11, 481);
    rel.d1.insert({d: some_date});
    checkEqualTo("rel.d1.field('d')", [some_date]);
    db.createRel('d2', {d: date}, constr.foreignKey('d', 'd1', 'd'));
    checkThrows(function () { rel.d2.insert({d: other_date}); });
    rel.d1.insert({d: other_date});
    checkEqualTo("rel.d1.by('-d').field('d')", [some_date, other_date]);
    rel.d2.insert({d: other_date});
    db.dropRels('d1', 'd2');
};


db_test_suite.testDefault = function()
{
    var now = new Date();
    db.createRel('def', {n: number.byDefault(42),
                         s: string.byDefault('hello, world!'),
                         b: boolean.byDefault(true),
                         d: date.byDefault(now)});
    checkEqualTo("items(rel.def.getDefaults()).sort()",
                 [['b', true],
                  ['d', now],
                  ['n', 42],
                  ['s', 'hello, world!']]);
    rel.def.insert({});
    checkThrows("rel.def.insert({})");
    rel.def.insert({b: false});
    rel.def.insert({n: 0, s: 'hi'});
    checkEqualTo("rel.def.by('b').by('n')",
                 [{b: false, d: now, n: 42, s: 'hello, world!'},
                  {b: true, d: now, n: 0, s: 'hi'},
                  {b: true, d: now, n: 42, s: 'hello, world!'}]);
    rel.def.drop();
};


db_test_suite.testIntSerial = function()
{
    checkThrows("number.serial().byDefault(42)");
    checkThrows("number.byDefault(42).serial()");
    checkThrows("number.serial().int()");
    checkThrows("number.serial().serial()");
    checkThrows("number.int().int()");
    checkEqualTo("rel.Comment.getInts().sort()", ['author', 'id', 'post']);
    db.createRel('r', {x: number.serial(),
                       y: number.serial(),
                       z: number.int()});
    checkEqualTo("rel.r.getInts().sort()", ['x', 'y', 'z']);
    checkEqualTo("rel.r.getSerials().sort()", ['x', 'y']);
    rel.r.drop();
};


db_test_suite.testUnique = function()
{
    db.createRel('r',
                 {a: number, b: string, c: boolean},
                 constr.unique('a', 'b'),
                 constr.unique('b', 'c'),
                 constr.unique('c'));
    checkEqualTo("rel.r.getUniques().sort()",
                 [['a', 'b'], ['a', 'b', 'c'], ['b', 'c'], ['c']]);
    checkEqualTo("rel.Dummy.getUniques()", [['id']]);
    rel.r.drop();
};


db_test_suite.testForeignKey = function()
{
    db.createRel('r',
                 {title: string,
                  author: number.int(),
                  id: number.serial(),
                  ref: number.int()},
                 constr.foreignKey(['title', 'author'],
                                   'Post',
                                   ['title', 'author']),
                 constr.foreignKey('ref', 'r', 'id'),
                 constr.unique('id'));
    checkEqualTo(function () {
                     return map(function (fk) {
                                    return items(fk).sort();
                                },
                                rel.r.getForeignKeys()).sort();
                 },
                 [[["key_fields", ["ref"]],
                   ["ref_fields", ["id"]],
                   ["ref_rel", "r"]],
                  [["key_fields", ["title", "author"]],
                   ["ref_fields", ["title", "author"]],
                   ["ref_rel", "Post"]]]);
    rel.r.drop();
};

////////////////////////////////////////////////////////////////////////////////
// Init tests
////////////////////////////////////////////////////////////////////////////////

(function ()
{
    try {
        include('bug-includer.js');
    } catch (err) {
        if (err instanceof Error)
            return;
        throw err;
    }
    throw Error("include('bug-includer.js') hasn't thrown");
})();


(function ()
{
    try {
        db.query();
    } catch (err) {
        if (err instanceof Error)
            return;
        throw err;
    }
    throw Error("db.query() hasn't thrown");
})();

////////////////////////////////////////////////////////////////////////////////
// File test suite
////////////////////////////////////////////////////////////////////////////////

var file_test_suite = {};


file_test_suite.setUp = function ()
{
    forEach(ak.fs.list(''), ak.fs.remove);
    fs.mkdir('dir1');
    fs.mkdir('dir2');
    fs.mkdir('dir1/subdir');
    fs.write('file', 'some text');
    fs.write('dir1/subdir/hello', 'hello world!');
};


file_test_suite.tearDown = function ()
{
    forEach(ak.fs.list(''), ak.fs.remove);
};


file_test_suite.testRead = function ()
{
    checkEqualTo("fs.read('//dir1////subdir/hello')",
                 'hello world!');
    checkThrows("fs.read('does_not_exists')");
    checkThrows("fs.read('dir1')");
    checkThrows("fs.read('//..//test_app/dir1/subdir/hello')");
    checkThrows("fs.read('////')");
    checkThrows(function () {
                    var path = '';
                    for (var i = 0; i < 40; ++i)
                        path += '/dir';
                    fs.read(path);
                });
};


file_test_suite.testList = function ()
{
    checkEqualTo("fs.list('').sort()", ['dir1', 'dir2', 'file']);
};


file_test_suite.testExists = function ()
{
    checkEqualTo("fs.exists('')", true);
    checkEqualTo("fs.exists('dir1/subdir/hello')", true);
    checkEqualTo("fs.exists('no/such')", false);
};


file_test_suite.testIsDir = function ()
{
    checkEqualTo("fs.isDir('')", true);
    checkEqualTo("fs.isDir('dir2')", true);
    checkEqualTo("fs.isDir('file')", false);
    checkEqualTo("fs.isDir('no/such')", false);
};


file_test_suite.testIsFile = function ()
{
    checkEqualTo("fs.isFile('')", false);
    checkEqualTo("fs.isFile('dir1/subdir/hello')", true);
    checkEqualTo("fs.isFile('dir1/subdir')", false);
    checkEqualTo("fs.isFile('no/such')", false);
};


file_test_suite.testRm = function ()
{
    fs.write('new-file', 'data');
    fs.rm('new-file');
    fs.mkdir('dir2/new-dir');
    fs.rm('dir2/new-dir');
    checkEqualTo("fs.list('').sort()", ['dir1', 'dir2', 'file']);
    checkEqualTo("fs.list('dir2')", []);
};


file_test_suite.testWrite = function ()
{
    fs.write('wuzzup', 'yo wuzzup!');
    checkEqualTo("fs.read('wuzzup')", 'yo wuzzup!');
    fs.write('hello', fs.read('dir1/subdir/hello'));
    checkEqualTo("fs.read('hello')", 'hello world!');
    fs.rm('wuzzup');
};


file_test_suite.testMkDir = function ()
{
    fs.mkdir('dir2/ddd');
    checkEqualTo("fs.list('dir2')", ['ddd']);
    checkEqualTo("fs.list('dir2/ddd')", []);
    fs.rm('dir2/ddd');
};


file_test_suite.testCopyFile = function ()
{
    fs.copyFile('dir1/subdir/hello', 'dir2/hello');
    checkEqualTo("fs.read('dir2/hello')", 'hello world!');
    fs.rm('dir2/hello');
    checkThrows("fs.copyFile('no_such', 'never_created')");
    checkThrows("fs.copyFile('file', 'dir1/subdir')");
};


file_test_suite.testRename = function ()
{
    fs.rename('dir1', 'dir2/dir3');
    checkEqualTo("fs.read('dir2/dir3/subdir/hello')", 'hello world!');
    fs.rename('dir2/dir3', 'dir1');
};

////////////////////////////////////////////////////////////////////////////////
// File test suite
////////////////////////////////////////////////////////////////////////////////

var call_test_suite = {};


call_test_suite.testCall = function ()
{
    fs.write('file1', 'wuzzup');
    fs.write('file2', 'yo ho ho');
    checkEqualTo(function () {
                     return apps.another_app.call('hello world!',
                                                  ['file1', 'file2'],
                                                  'yo!!!');
                 },
                 '{"arg":"hello world!","data":"yo!!!",' +
                 '"file_contents":["wuzzup","yo ho ho"]}');
    check("!fs.exists('file1') && !fs.exists('file2')");
    checkThrows("apps.no_such_app.call('hi')");
    check("'another_app' in apps");
    check("!('no_such_app' in apps)");
    checkThrows("apps.another_app.call('', {length: 1})");
    checkEqualTo(
        function () {
            fs.write('file3', 'text');
            var result = apps.another_app.call('', [], fs.read('file3'));
            fs.remove('file3');
            return result;
        },
        '{"arg":"","data":"text","file_contents":[]}');
    checkThrows("apps['invalid/app/name'].call('')");
    checkThrows("apps.test_app.call('2+2')");
    checkThrows("apps.throwing_app.call('')");
    checkThrows("apps.blocking_app.call('')");
    checkThrows("apps.another_app.call('', ['..'])");
    checkThrows("apps.another_app.call('', ['no-such-file'])");
    checkThrows(function () {
                    fs.mkdir('dir');
                    try {
                        apps.another_app.call('', ['dir']);
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
