// (c) 2009-2011 by Anton Korenyushkin

var imports = {
  core: [
    'print',
    'set',
    'hash',
    'construct',
    'ValueError',
    'NotImplementedError',
    'QuotaError',
    'RequireError',
    'COMMON',
    'READONLY',
    'HIDDEN',
    'PERMANENT'
  ],
  db: [
    'DBError',
    'RelVarExistsError',
    'NoSuchRelVarError',
    'AttrExistsError',
    'NoSuchAttrError',
    'ConstraintError',
    'QueryError',
    'DependencyError'
  ],
  fs: [
    'code',
    'lib',
    'FSError',
    'EntryExistsError',
    'NoSuchEntryError',
    'EntryIsFolderError',
    'EntryIsFileError'
  ],
  binary: [
    'Binary',
    'ConversionError'
  ],
  proxy: [
    'Proxy'
  ],
  script: [
    'Script'
  ],
  socket: [
    'connect',
    'Socket',
    'SocketError'
  ],
  'http-parser': [
    'HttpParser'
  ],
  git: [
    'Repo',
    'GitStorage'
  ]
};


for (var moduleName in imports) {
  this[moduleName] = require(moduleName);
  imports[moduleName].forEach(
    function (importName) {
      this[importName] = this[moduleName][importName];
    });
}

require('jsgi');
var jsgiHandle = exports.handle;

////////////////////////////////////////////////////////////////////////////////
// Utils
////////////////////////////////////////////////////////////////////////////////

function field(name, str, queryParams, by, byParams, start, length) {
  return Array.prototype.map.call(
    db.query('for (x in ' + str + ') x.' + name,
             queryParams, by, byParams, start, length),
    function (item) { return item[name]; });
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
  suites.forEach(
    function (suite) {
      function run(name) {
        if (!(name in suite))
          return true;
        try {
          suite[name]();
          return true;
        } catch (error) {
          ++errorCount;
          print(error.stack + '\n');
          return false;
        }
      }
      for (var name in suite) {
        if (name.substr(0, 4) != 'test')
          continue;
        ++testCount;
        if (!run('setUp'))
          continue;
        run(name);
        run('tearDown');
      }
    });
  print(errorCount + ' errors detected in ' +
        testCount + ' test cases in ' +
        suites.length + ' test suites\n');
  return errorCount;
}

////////////////////////////////////////////////////////////////////////////////
// core
////////////////////////////////////////////////////////////////////////////////

var coreTestSuite = {
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
      'error'
    ].forEach(
      function (name) {
        assert(require(name + '/index').pass);
      });
    var m = require('main/index');
    assertEqual(
      items(module),
      [['id', 'main'], ['exports', exports], ['storage', fs.code]]);
    assertSame(require.main, module);
    assertSame(m.main, module);
    assertSame(require('default', 'core'), core);
    assertThrow(RequireError, require, 'noSuch');
    assertThrow(RequireError, require, './core');
    assertThrow(RequireError, require, 'noSuch', 'module');
    assertThrow(RequireError, require, 'default', 'noSuch');

    var libs = {
      anton: {
        core: {
          '0.1': {
            'index.js': 'exports.x = 42'
          }
        }
      },
      akshell: {
        fs: {
          '0.1': {}
        },
        ak: {
          '0.3': {
            'manifest.json': '{libs: {fs: "akshell/fs:0.1"}}',
            'index.js': 'exports.fs = require("fs")',
            'error.js': 'throw Error()'
          },
          '0.2': {
            'manifest.json': '{libs: {core: "anton/core:0.1"}}',
            'index.js':
            'require("default", "core").set(exports, "x", 0, require("core").x)'
          }
        },
        form: {
          '0.2': {
            'manifest.json': '{libs: {ak: "akshell/ak:0.2"}}',
            'dir/x.js': 'exports.x = require("ak").x'
          }
        }
      },
      other: {
        bad: {
          '0.1': {
            'manifest.json': '{',
            'index.js': ''
          }
        }
      }
    };
    var MockRepo = function (ownerName, libName) {
      assert(libs.hasOwnProperty(ownerName));
      assert(libs[ownerName].hasOwnProperty(libName));
      this.lib = libs[ownerName][libName];
    };
    MockRepo.prototype.getStorage = function (libVersion) {
      assert(this.lib.hasOwnProperty(libVersion));
      return new MockStorage(this.lib[libVersion]);
    };
    var MockStorage = function (files) {
      this.files = files;
    };
    MockStorage.prototype.read = function (path) {
      if (!this.files.hasOwnProperty(path))
        throw NoSuchEntryError();
      return new Binary(this.files[path]);
    };
    git.Repo = MockRepo;
    try {
      assertSame(require('form', 'dir/x').x, 42);
      assertSame(require('form', 'dir/x').x, 42);
      assertSame(require('ak').fs, fs);
      assertThrow(RequireError, require, 'bad');
      assertThrow(RequireError, require, 'incorrect');
      try {
        require('ak', 'error');
      } catch (error) {
        assert(error.stack.indexOf('akshell/ak:0.3:error.js:1:7') != -1);
      }
    } finally {
      git.Repo = Repo;
    }
  },

  testSet: function () {
    var obj = {};
    assertThrow(TypeError, set);
    assertThrow(TypeError, set, 1, 'f', COMMON, 42);
    assertSame(set(obj, 'readOnly', READONLY, 1), obj);
    set(obj, 'dontEnum', HIDDEN, 2);
    set(obj, 'dontDelete', PERMANENT, 3);
    assertThrow(TypeError, set, obj, 'field', {}, 42);
    assertThrow(ValueError, set, obj, 'field', 8, 42);
    obj.readOnly = 5;
    assertSame(obj.readOnly, 1);
    assertEqual(keys(obj), ['readOnly', 'dontDelete']);
    delete obj.dontDelete;
    assertSame(obj.dontDelete, 3);
  },

  testHash: function () {
    assertThrow(TypeError, hash);
    assertSame(hash(undefined), 0);
    assertSame(hash(null), 0);
    assertSame(hash(42), 0);
    assertSame(hash('foo'), 0);
    assertSame(hash(''), 0);
    assert(hash({}) > 0);
    assert(hash(function () {}) > 0);
  },

  testConstruct: function () {
    var binary = construct(Binary, [new Binary('hello '), new Binary('world')]);
    assertSame(binary + '', 'hello world');
    assertThrow(TypeError, construct, 42, []);
  },

  testErrors: function () {
    assertSame(ValueError.prototype.name, 'ValueError');
    assert(new NoSuchAttrError() instanceof DBError);
    assert(ValueError() instanceof ValueError);
    assertSame(NotImplementedError(42).message, '42');
  }
};

////////////////////////////////////////////////////////////////////////////////
// db
////////////////////////////////////////////////////////////////////////////////

var dbTestSuite1 = {
  setUp: function () {
    db.dropAll();

    db.create('Empty', {});
    db.create(
      'User',
      {
        id: 'serial',
        name: 'string',
        age: 'number',
        flooder: ['boolean', 'yo!']
      },
      [['id'], ['name']]);
    db.create(
      'Post',
      {
        id: 'serial',
        title: 'string',
        text: 'string',
        author: 'integer'
      },
      [['id'], ['title', 'author']],
      [[['author'], 'User', ['id']]]);
    db.create(
      'Comment',
      {
        id: 'serial',
        text: 'string',
        author: 'integer',
        post: 'integer'
      },
      [['id']],
      [[['author'], 'User', ['id']], [['post'], 'Post', ['id']]]);

    db.insert('User', {name: 'anton', age: 22, flooder: 15});
    db.insert('User', {name: 'marina', age: 25, flooder: false});
    db.insert('User', {name: 'den', age: 23});

    db.insert('Post', {title: 'first', text: 'hello world', author: 0});
    db.insert('Post', {title: 'second', text: 'hi', author: 1});
    db.insert('Post', {title: 'third', text: 'yo!', author: 0});

    db.insert('Comment', {text: 42, author: 1, post: 0});
    db.insert('Comment', {text: 'rrr', author: 0, post: 0});
    db.insert('Comment', {text: 'ololo', author: 2, post: 2});

    db.create('Count', {i: 'number'});
    for (var i = 0; i < 10; ++i)
      db.insert('Count', {i: i});
  },

  tearDown: function () {
    db.dropAll();
  },

  testList: function () {
    assertEqual(db.list(), ['Comment', 'Count', 'Empty', 'Post', 'User']);
  },

  testCreate: function () {
    assertThrow(TypeError, "db.create('Bad')");
    assertThrow(ValueError, db.create, '', {});
    assertThrow(ValueError, db.create, '123bad', {});
    assertThrow(ValueError, db.create, 'Bad', {'_@': 'number'});
    assertThrow(TypeError, db.create, 'Bad', 'str');
    assertThrow(TypeError, "db.create('Bad', {}, 'str', [], [])");
    assertThrow(ValueError, db.create, 'Bad', {attr: 15});
    assertThrow(ValueError, db.create, 'Bad', {attr: []});
    function E() {}
    assertThrow(E, db.create, 'RV', {get x() { throw new E(); }});
    assertThrow(RelVarExistsError, db.create, 'User', {});

    assertThrow(ValueError,
                db.create, 'Bad', {x: 'number'}, [[]]);
    assertThrow(ValueError,
                db.create,
                'Bad', {x: 'number', y: 'number'},
                [], [['x', 'y'], 'User', ['id']]);
    assertThrow(TypeError,
                db.create,
                'Bad', {'id': 'number'},
                [], [[['id'], 'Post', 'id']]);
    assertThrow(ConstraintError,
                db.create,
                'Bad', {x: 'number', y: 'number'},
                [], [[['x', 'y'], 'Post', ['id', 'author']]]);
    assertThrow(QuotaError,
                function () {
                  var name = '';
                  for (var i = 0; i < 61; ++i)
                    name += 'x';
                  db.create(name, {});
                });
    assertThrow(QuotaError,
                function () {
                  attrs = {};
                  for (var i = 0; i < 1000; ++i)
                    attrs['attr' + i] = 'number';
                  db.create('Bad', attrs);
                });

    db.create('X', {b: ['boolean', new Date()]});
    db.insert('X', {});
    assertSame(db.query('X')[0].b, true);

    assertThrow(TypeError, db.create, 'Bad', {d: ['date', 'bad']});
    assertThrow(ValueError, db.create, 'Bad', {}, [], [['a', 'b']]);
    assertThrow(ValueError, db.create, 'Bad', {a: 'number'}, [['a', 'a']]);
    assertThrow(ValueError,
                db.create, 'Bad', {x: 'number'}, [], [[[], 'User', []]]);
    assertThrow(ConstraintError,
                db.create, 'Bad', {x: 'number'},
                [], [[['x'], 'User', ['age']]]);
    assertThrow(ValueError,
                db.create, 'Bad', {x: 'number'},
                [], [[['x'], 'User', ['id', 'age']]]);
  },

  testDropRelVars: function () {
    db.drop(['Empty']);
    assertSame(db.list().indexOf('NewRelVar'), -1);
    assertThrow(DependencyError, "db.drop(['User'])");
    assertThrow(DependencyError, "db.drop(['User', 'Post'])");
    assertThrow(ValueError, "db.drop(['Comment', 'Comment'])");
    db.drop(['Post', 'Comment']);
    assertThrow(NoSuchRelVarError, "db.drop(['User', 'NoSuch'])");
  },

  testQuery: function () {
    assertThrow(TypeError, "db.query()");
    assertThrow(TypeError, db.query, 'User', {});
    assertEqual(
      db.query('User[name, age, flooder] where +id == "0"').map(items),
      [[['name', 'anton'], ['age', 22], ['flooder', true]]]);
    assertThrow(NoSuchRelVarError, db.query, 'dfsa');
    assertEqual(
      db.query('Post.author->name where id == $', [0]).map(items),
      [[['name', 'anton']]]);
    assertThrow(NoSuchAttrError, db.query, 'User.asdf');
    assertEqual(
      db.query('User[id, name] where id == $1 && name == $2',
               [0, 'anton']).map(items),
      [[['id', 0], ['name', 'anton']]]);
    assertSame(db.query('User where forsome (x in {}) true').length, 3);
    assertThrow(QueryError, db.query, '{i: 1} where i->name');
    assertEqual(
      field('title', ' Post where author->name == $', ['anton']).sort(),
      ['first', 'third']);
    assertEqual(field('age', 'User where name == \'anton\''), [22]);
    assertEqual(field('age', 'User where name == "den"'), [23]);
    assertThrow(QueryError, db.query, 'for (x in {f: 1}) x.f->k');
    assertThrow(QueryError, db.query, '{f: 1}', [], ['f->k']);
    assertEqual(db.query('{} where $1 == $2', [false, new Date()]), []);
    assertEqual(
      db.query(('User[id, age] where ' +
                'flooder && ' +
                '(forsome (Comment) ' +
                ' author == User.id && post->author->name == $)'),
               ['anton'],
               ['id']).map(items),
      [[['id', 0], ['age', 22]], [['id', 2], ['age', 23]]]);
  },

  testInsert: function () {
    assertThrow(TypeError, "db.insert('User')");
    assertThrow(TypeError, "db.insert('User', 15)");
    assertThrow(NoSuchAttrError, "db.insert('User', {'@': 'abc'})");
    assertThrow(
      ConstraintError,
      "db.insert('Comment', {id: 2, text: 'yo', author: 5, post: 0})");
    assertThrow(ValueError, "db.insert('User', {id: 2})");
    assertThrow(NoSuchAttrError, "db.insert('Empty', {x: 5})");
    assertEqual(
      items(db.insert('User', {name: 'xxx', age: false})),
      [['id', 3], ['name', 'xxx'], ['age', 0], ['flooder', true]]);
    var tuple = db.insert('User', {id: 4, name: 'yyy', age: 'asdf'});
    assert(isNaN(tuple.age));
    assertThrow(ConstraintError,
                "db.insert('User', {id: 'asdf', name: 'zzz', age: 42})");
    assertThrow(ConstraintError,
                "db.insert('User', {name: 'zzz', age: 42})");
    assertEqual(items(db.insert('Empty', {})), []);
    assertEqual(db.query('Empty').map(items), [[]]);
    assertThrow(ConstraintError, "db.insert('Empty', {})");
    db.create('Num', {n: 'number', i: ['integer', 3.14]});
    assertSame(db.insert('Num', {n: 0}).i, 3);
    assertSame(db.insert('Num', {n: 1.5, i: 1.5}).i, 2);
    assertSame(db.insert('Num', {n: Infinity}).n, Infinity);
    assertSame(db.insert('Num', {n: -Infinity}).n, -Infinity);
    assert(isNaN(db.insert('Num', {n: NaN}).n));
    assertThrow(ConstraintError, "db.insert('Num', {n: 0, i: Infinity})");
    assertThrow(ConstraintError, "db.insert('Num', {n: 0, i: -Infinity})");
    assertThrow(ConstraintError, "db.insert('Num', {n: 0, i: NaN})");
  },

  testGetHeader: function () {
    assertEqual(
      items(db.getHeader('User')),
      [
        ['id', 'serial'],
        ['name', 'string'],
        ['age', 'number'],
        ['flooder', 'boolean']
      ]);
    assertEqual(
      items(db.getHeader('Post')),
      [
        ['id', 'serial'],
        ['title', 'string'],
        ['text', 'string'],
        ['author', 'integer']
      ]);
  },

  testStartLength: function () {
    assertEqual(field('i', 'Count', [], ['i'], [], 8), [8, 9]);
    assertEqual(field('i', 'Count', [], ['i'], [], 6, 2), [6, 7]);
    assertEqual(field('i', 'Count', [], ['i'], [], 10), []);
    assertEqual(field('i', 'Count where i != 5', [], ['i'], [], 1, 6),
                [1, 2, 3, 4, 6, 7]);
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
    assertSame(db.query('User where id == $', [0])[0]['name'], 'anton');
    assertEqual(field('id', 'User where name != $', ['den'], ['-id']), [1, 0]);
  },

  testUpdate: function () {
    assertThrow(ValueError, "db.update('User', 'id == 0', [], {}, [])");
    assertThrow(TypeError, "db.update('User', 'id == 0')");
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
        "db.update('User', 'name == $', ['den'], {id: '4'})");
  },

  testDel: function () {
    assertThrow(ConstraintError, "db.del('User', 'true', [])");
    var name = "xx'y'zz'";
    db.insert('User', {id: 3, name: name, age: 15, flooder: true});
    db.insert('Post', {id: 3, title: "", text: "", author: 3});
    db.update('User', 'id == 3', [], {name: 'name + 1'}, []);
    assertEqual(field('name', 'User where id == 3'), [name + 1]);
    assertSame(db.del('Post', 'author->name == $', [name + 1]), 1);
    assertSame(db.del('User', 'age == $', [15]), 1);
    assertEqual(field('id', 'User', [], ['id']), [0, 1, 2]);
    assertSame(db.del('Comment', 'true'), 3);
  }
};


var dbTestSuite2 = {
  setUp: function () {
    db.dropAll();
  },

  tearDown: function () {
    db.dropAll();
  },

  testBy: function () {
    db.create('ByTest', {x: 'number', y: 'number'});
    db.insert('ByTest', {x: 0, y: 0});
    db.insert('ByTest', {x: 1, y: 14});
    db.insert('ByTest', {x: 2, y: 21});
    db.insert('ByTest', {x: 3, y: 0});
    db.insert('ByTest', {x: 4, y: 0});
    db.insert('ByTest', {x: 5, y: 5});
    db.insert('ByTest', {x: 8, y: 3});
    db.insert('ByTest', {x: 9, y: 32});
    assertEqual(
      db.query('ByTest where y != $',
               [5],
               ['x * $1 % $2', 'y % $3'],
               [2, 7, 10],
               3, 3).map(items),
      [[['x', 1], ['y', 14]], [['x', 2], ['y', 21]], [['x', 9], ['y', 32]]]);
    assertEqual(field('x', 'ByTest', [], ['x'], [], 5), [5, 8, 9]);
    assertEqual(field('x', 'ByTest', [], ['x'], [], 0, 2), [0, 1]);
    assertSame(db.query('ByTest', [], [], [], 3, 6).length, 5);
  },

  testPg: function () {
    db.create('pg_class', {x: 'number'});
    db.insert('pg_class', {x: 0});
    assertEqual(db.query('pg_class').map(items), [[['x', 0]]]);
    db.drop(['pg_class']);
  },

  testCheck: function () {
    db.create('X', {n: 'number'}, [], [], ['n != 42']);
    db.create('Y', {b: 'boolean', s: 'string'}, [], [], ['b || s == "hello"']);
    db.insert('X', {n: 0});
    assertThrow(ConstraintError, "db.insert('X', {n: 42})");
    db.insert('Y', {b: true, s: 'hi'});
    db.insert('Y', {b: false, s: 'hello'});
    assertThrow(ConstraintError,
                "db.insert('Y', {b: false, s: 'oops'})");
  },

  testDate: function () {
    db.create('D1', {d: 'date'});
    var someDate = new Date('Wed Mar 04 2009 16:12:09');
    var otherDate = new Date(2009, 0, 15, 13, 27, 11, 481);
    db.insert('D1', {d: someDate});
    assertSame(db.query('{s: D1.d + ""}')[0].s, 'Wed Mar 04 2009 16:12:09');
    assertEqual(field('d', 'D1'), [someDate]);
    db.create('D2', {d: 'date'}, [], [[['d'], 'D1', ['d']]]);
    assertThrow(ConstraintError,
                function () { db.insert('D2', {d: otherDate}); });
    db.insert('D1', {d: otherDate});
    assertEqual(field('d', 'D1', [], ['-d']), [someDate, otherDate]);
    db.insert('D2', {d: otherDate});
    assertThrow(TypeError, "db.insert('D1', {d: new Date('invalid')})");
    assertThrow(TypeError, "db.insert('D1', {d: 'invalid'})");
    assertThrow(TypeError, "db.insert('D1', {d: 42})");
    assertThrow(TypeError, "db.update('D1', 'true', [], {d: 'd + 1'}, [])");
  },

  testDefault: function () {
    var now = new Date();
    db.create('X',
              {
                n: ['number', 42],
                s: ['string', 'hello, world!'],
                b: ['boolean', true],
                d: ['date', now]
              });
    assertEqual(items(db.getDefault('X')).sort(),
                [['b', true],
                 ['d', now],
                 ['n', 42],
                 ['s', 'hello, world!']]);
    db.insert('X', {});
    assertThrow(ConstraintError, "db.insert('X', {})");
    db.insert('X', {b: false});
    db.insert('X', {n: 0, s: 'hi'});
    assertEqual(db.query('X', [], ['b', 'n']).map(items),
                [[['n', 42], ['s', 'hello, world!'], ['b', false], ['d', now]],
                 [['n', 0], ['s', 'hi'], ['b', true], ['d', now]],
                 [['n', 42], ['s', 'hello, world!'], ['b', true], ['d', now]]]);
  },

  testUnique: function () {
    db.create('X',
              {a: 'number', b: 'string', c: 'boolean'},
              [['a', 'b'], ['b', 'c'], ['c']]);
    assertEqual(db.getUnique('X').sort(), [['a', 'b'], ['b', 'c'], ['c']]);
    db.create('Y', {n: 'number'});
    assertEqual(db.getUnique('Y'), [['n']]);
  },

  testForeignKey: function () {
    db.create('X', {s: 'string', i: 'integer'});
    db.create('Y',
              {
                s: 'string',
                i: 'integer',
                id: 'serial',
                ref: 'integer'
              },
              [['id']],
              [
                [['s', 'i'], 'X', ['s', 'i']],
                [['ref'], 'Y', ['id']]
              ]);
    assertEqual(db.getForeign('Y').sort(),
                [
                  [['ref'], 'Y', ['id']],
                  [['s', 'i'], 'X', ['s', 'i']]
                ]);
  },

  testRelVarNumber: function () {
    assertThrow(QuotaError,
                function () {
                  for (var i = 0; i < 501; ++i)
                    db.create('X' + i, {});
                });
  },

  testBigIndexRow: function () {
    db.create('X', {s: 'string'});
    var s = 'x';
    for (var i = 0; i < 20; ++i)
      s += s;
    assertThrow(DBError, function () { db.insert('X', {s: s}); });
  },

  testAddAttrs: function () {
    assertThrow(TypeError, "db.addAttrs('X', 42)");
    assertThrow(ValueError, "db.addAttrs('X', {x: []})");
    assertThrow(ValueError, "db.addAttrs('X', {x: [1, 2]})");
    assertThrow(NotImplementedError,
                "db.addAttrs('X', {id: ['serial', 0]})");
    db.create('X', {id: 'serial'});
    db.insert('X', {});
    db.insert('X', {});
    var d = new Date();
    db.addAttrs('X', {});
    db.addAttrs(
      'X',
      {
        n: ['number', 4.2],
        i: ['integer', 0.1],
        s: ['string', 'yo'],
        d: ['date', d]
      });
    assertEqual(
      db.query('X', [], ['id']).map(items),
      [
        [['id', 0], ['n', 4.2], ['i', 0], ['s', 'yo'], ['d', d]],
        [['id', 1], ['n', 4.2], ['i', 0], ['s', 'yo'], ['d', d]]
      ]);
    assertThrow(AttrExistsError, "db.addAttrs('X', {s: ['string', '']})");
    for (var i = 0; i < 495; ++i) {
      var descr = {};
      descr['a' + i] = ['number', i];
      db.addAttrs('X', descr);
    }
    assertThrow(QuotaError, "db.addAttrs('X', {another: ['string', '']})");
    db.create('Y', {});
    db.insert('Y', {});
    db.addAttrs('Y', {n: ['number', 0], s: ['string', '']});
    assertEqual(db.getUnique('Y'), [['n', 's']]);
    assertThrow(ConstraintError, "db.insert('Y', {n: 0, s: ''})");
  },

  testDropAttrs: function () {
    db.create('X',
              {n: 'number', s: 'string', b: 'boolean', d: 'date'},
              [['n'], ['s', 'b']]);
    db.create('Y',
              {n: 'number', s: 'string', b: 'boolean', d: 'date'},
              [], [[['n'], 'X', ['n']], [['s', 'b'], 'X', ['s', 'b']]]);
    assertThrow(NoSuchAttrError, "db.dropAttrs('X', ['n', 'x'])");
    assertThrow(DependencyError, "db.dropAttrs('X', ['b', 'd'])");
    db.dropAttrs('X', ['d']);
    assertEqual(db.getUnique('X'), [['n'], ['s', 'b']]);
    db.dropAttrs('Y', []);
    db.insert('X', {n: 0, s: '', b: false});
    db.insert('X', {n: 1, s: '', b: true});
    db.insert('Y', {n: 0, s: '', b: true, d: new Date()});
    db.insert('Y', {n: 0, s: '', b: false, d: new Date()});
    assertThrow(ConstraintError, "db.dropAttrs('Y', ['b', 'd'])");
    db.del('Y', 'b', []);
    db.dropAttrs('Y', ['b', 'd']);
    assertEqual(items(db.getHeader('Y')).sort(),
                [["n", "number"], ["s", "string"]]);
    assertEqual(db.getForeign('Y'), [[['n'], 'X', ['n']]]);
    assertEqual(db.getUnique('Y'), [['n', 's']]);
    db.insert('Y', {n: 1, s: ''});
    db.dropAttrs('Y', ['n', 's']);
    assertSame(db.query('Y').length, 1);
    db.del('X', 'true', []);
    db.dropAttrs('X', ['n', 's', 'b']);
    assertSame(db.query('X').length, 0);
    assertEqual(db.getUnique('X'), []);
  },

  testAddDefault: function () {
    db.create('X', {n: 'number', s: 'string', b: 'boolean'});
    assertThrow(NoSuchAttrError, "db.addDefault('X', {s: '', x: 42})");
    db.addDefault('X', {});
    assertEqual(items(db.getDefault('X')), []);
    db.addDefault('X', {n: 42, b: true});
    assertEqual(items(db.insert('X', {s: 'the answer'})),
                [['n', 42], ['s', 'the answer'], ['b', true]]);
    db.addDefault('X', {s: 'yo', b: false});
    assertEqual(items(db.insert('X', {})),
                [['n', 42], ['s', 'yo'], ['b', false]]);
    assertEqual(items(db.getDefault('X')),
                [['n', 42], ['s', 'yo'], ['b', false]]);
  },

  testDropDefault: function () {
    db.create(
      'X',
      {
        n: ['number', 42],
        s: ['string', ''],
        b: ['boolean', false]
      });
    assertThrow(NoSuchAttrError, "db.dropDefault('X', ['a', 's'])");
    db.dropDefault('X', []);
    db.dropDefault('X', ['s', 'b']);
    assertEqual(items(db.getDefault('X')), [['n', 42]]);
    db.insert('X', {s: "", b: false});
    assertThrow(DBError, "db.dropDefault('X', ['n', 's'])");
    db.dropDefault('X', ['n']);
    assertEqual(items(db.getDefault('X')), []);
  },

  testAddConstrs: function () {
    db.create('X', {n: 'number', s: 'string'}, [['n'], ['n'], ['s']]);
    assertEqual(db.getUnique('X'), [['n'], ['s']]);
    db.create('Y', {n: 'number', s: 'string', b: 'boolean'});
    db.addConstrs('Y');
    db.addConstrs('Y', [['n']], [[['s'], 'X', ['s']]], ['b']);
    assertEqual(db.getUnique('Y'), [['n', 's', 'b'], ['n']]);
    assertEqual(db.getForeign('Y'), [[['s'], 'X', ['s']]]);
    db.insert('X', {n: 0, s: ''});
    db.insert('X', {n: 1, s: 'yo'});
    db.insert('Y', {n: 0, s: '', b: true});
    assertThrow(ConstraintError, "db.insert('Y', {n: 0, s: 'yo', b: true})");
    assertThrow(ConstraintError, "db.insert('Y', {n: 1, s: 'hi', b: true})");
    assertThrow(ConstraintError, "db.insert('Y', {n: 1, s: '', b: false})");
    db.addConstrs('Y', [['n']], [], ['n != 42', 'b']);
    assertEqual(db.getUnique('Y'), [['n', 's', 'b'], ['n']]);
    assertThrow(ConstraintError, "db.insert('Y', {n: 42, s: '', b: true})");
    db.insert('Y', {n: 2, s: '', b: true});
    assertThrow(ConstraintError, "db.addConstrs('Y', [['s']], [], [])");
    assertThrow(ConstraintError,
                "db.addConstrs('Y', [], [[['n'], 'X', ['n']]], [])");
    assertThrow(ConstraintError, "db.addConstrs('Y', [], [], ['!b'])");
  },

  testDropAllConstrs: function () {
    db.create('E', {});
    db.dropAllConstrs('E');
    db.create('X', {n: 'number'});
    db.create(
      'Y',
      {
        n: 'number',
        s: 'string',
        c: 'boolean'
      },
      [['s']],
      [[['n'], 'X', ['n']]],
      ['c']);
    assertThrow(DependencyError, "db.dropAllConstrs('X')");
    db.dropAllConstrs('Y');
    assertEqual(db.getUnique('Y'), [['n', 's', 'c']]);
    assertEqual(db.getForeign('Y'), []);
    db.insert('Y', {n: 0, s: '', c: false});
    db.dropAllConstrs('X');
    db.create('Z', {n: 'number', s: 'string'}, [['n']]);
    var s = 'x';
    for (var i = 0; i < 20; ++i)
      s += s;
    db.insert('Z', {n: 0, s: s});
    assertThrow(QuotaError, "db.dropAllConstrs('Z')");
  },

  testJSON: function () {
    db.create('X', {j: 'json'});
    db.insert('X', {j: [1, 2, 3]});
    assertEqual(db.query('X')[0].j, [1, 2, 3]);
    db.insert('X', {j: 42});
    var tuples = db.query('{n: X.j + 0}');
    assertSame(tuples[0].n, 42);
    assert(isNaN(tuples[1].n));
    tuples = db.query('X where j < "["');
    assertSame(tuples.length, 1);
    assertSame(tuples[0].j, 42);
    assertThrow(TypeError, "db.update('X', 'true', [], {j: 'j + 1'}, [])");
    function E() {}
    assertThrow(
      E,
      function () {
        db.insert('X', {j: {toJSON: function () { throw new E(); }}});
      });
    assertThrow(TypeError, "db.insert('X', {j: undefined})");
  },

  testBinary: function () {
    db.create('X', {b: 'binary'});
    db.insert('X', {b: ''});
    db.insert('X', {b: 'hello'});
    db.insert('X', {b: new Binary([0, 0, 0])});
    assertSame(db.query('X where !b').length, 1);
    var tuples = db.query('X where b', [], 'b');
    assertSame(tuples.length, 2);
    assert(tuples[0].b instanceof Binary);
    assertSame(tuples[0].b + '', '\0\0\0');
    assertSame(tuples[1].b + '', 'hello');
    assertThrow(TypeError, db.query, "X where +b");
  }
};

////////////////////////////////////////////////////////////////////////////////
// fs
////////////////////////////////////////////////////////////////////////////////

var fsTestSuite = {
  testExists: function () {
    assertSame(code.exists(''), true);
    assertSame(code.exists('.//absolute/./..//main.js'), true);
    assertSame(code.exists('no/such'), false);
    assertSame(lib.exists('core.js'), true);
    assertThrow(ValueError, "code.exists('absolute/../../')");
  },

  testIsFolder: function () {
    assertSame(code.isFolder(''), true);
    assertSame(code.isFolder('absolute'), true);
    assertSame(code.isFolder('main.js'), false);
    assertSame(code.isFolder('no/such'), false);
  },

  testIsFile: function () {
    assertSame(code.isFile(''), false);
    assertSame(code.isFile('absolute/index.js'), true);
    assertSame(code.isFile('absolute/'), false);
    assertSame(code.isFile('no/such'), false);
  },

  testRead: function () {
    assertSame(code.read('absolute/b.js') + '',
               'exports.foo = function() {};\n');
    assertThrow(NoSuchEntryError, "code.read('no/such')");
    assertThrow(EntryIsFolderError, "code.read('absolute')");
  },

  testList: function () {
    assertEqual(code.list('absolute'), ['b.js', 'index.js', 'submodule']);
    assertThrow(NoSuchEntryError, "lib.list('no/such')");
    assertThrow(EntryIsFileError, "code.list('main.js')");
  }
};

////////////////////////////////////////////////////////////////////////////////
// git
////////////////////////////////////////////////////////////////////////////////

var gitTestSuite = {
  testRepo: function () {
    assertSame(Repo(), undefined);
    assertThrow(ValueError, "new Repo('code', 'bad/name')");
    assertThrow(ValueError, "new Repo('code', '')");
    assertThrow(ValueError, "new Repo('bad/name', 'lib')");
    assertThrow(ValueError, "new Repo('code', 'no-such-lib')");
    var repo = new Repo('code', 'lib');
    assertEqual(
      items(repo.refs).sort(),
      [
        [
          'HEAD',
          'b483d1775aedd21eaeb958219e26fa68f36f2e31'
        ],
        [
          'refs/heads/master',
          'b483d1775aedd21eaeb958219e26fa68f36f2e31'
        ],
        [
          'refs/remotes/origin/HEAD',
          'ref: refs/remotes/origin/master'
        ],
        [
          'refs/remotes/origin/master',
          'b483d1775aedd21eaeb958219e26fa68f36f2e31'
        ],
        [
          'refs/tags/dummy',
          'b53dffba67ee52511ad67fd95a1f140bdb691936'
        ]
      ]);
    assertThrow(ValueError, function () { repo.readObject('invalid'); });
    assertThrow(ValueError,
                function () { repo.readObject('xxxxxxxxxxxxxxxxxxxx'); });
    var object = repo.readObject('b53dffba67ee52511ad67fd95a1f140bdb691936');
    assertSame(object.type, 'tag');
    assertSame(
      object.data + '',
      ('object 4a7af2ca3dbc02a712a3415b6ec9f3694e35c37d\n' +
       'type commit\n' +
       'tag dummy\n' +
       'tagger korenyushkin <anton@akshell.com> 1278150273 +0400\n' +
       '\n' +
       'Dummy tag.\n'));
    object = repo.readObject('4a7af2ca3dbc02a712a3415b6ec9f3694e35c37d');
    assertSame(object.type, 'commit');
    assertSame(
      object.data + '',
      ('tree c2b28e85ec63083f158cc26b04c053d51c27d928\n' +
       'author korenyushkin <anton@akshell.com> 1278150258 +0400\n' +
       'committer korenyushkin <anton@akshell.com> 1278150258 +0400\n' +
       '\n' +
       'Initial commit.\n'));
    object = repo.readObject('c2b28e85ec63083f158cc26b04c053d51c27d928');
    assertSame(object.type, 'tree');
    assertSame(object.data.range(0, 13) + '', '100644 README');
    assertSame(object.data[13], 0);
    object = repo.readObject(object.data.range(14));
    assertSame(object.type, 'blob');
    assertSame(object.data + '', 'Akshell engine test library.\n');
  },

  testGitStorage: function () {
    var MockRepo = function (refs, files) {
      this.refs = refs;
      this._files = files;
    };
    MockRepo.prototype.__proto__ = Repo.prototype;
    MockRepo.prototype.readObject = function (sha) {
      if (!this._files.hasOwnProperty(sha))
        throw ValueError(sha);
      var array = this._files[sha];
      return {type: array[0], data: new Binary(array[1])};
    };

    var mockRepo = new MockRepo(
      {
        'HEAD': 'ref: refs/remotes/master',
        'refs/heads/master': 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',
        'refs/heads/upstream': 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb',
        'refs/remotes/master': 'ref: refs/heads/upstream',
        'refs/tags/upstream': 'cccccccccccccccccccccccccccccccccccccccc',
        'refs/tags/bad': 'dddddddddddddddddddddddddddddddddddddddd'
      },
      {
        'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa':
        ['commit', 'tree dddddddddddddddddddddddddddddddddddddddd\n'],
        'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb':
        ['commit', 'tree eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\n'],
        'cccccccccccccccccccccccccccccccccccccccc':
        ['tag', 'object aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n'],
        'dddddddddddddddddddddddddddddddddddddddd':
        ['tree',
         '40000 dir\0aaaaaaaaaaaaaaaaaaaa100644 file\0bbbbbbbbbbbbbbbbbbbb'],
        'eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee':
        ['tree',
         '40000 dir\0aaaaaaaaaaaaaaaaaaaa100644 file\0cccccccccccccccccccc'],
        'aaaaaaaaaaaaaaaaaaaa':
        ['tree', '40000 subdir\0dddddddddddddddddddd'],
        'bbbbbbbbbbbbbbbbbbbb':
        ['blob', 'hello world'],
        'cccccccccccccccccccc':
        ['blob', 'hi there'],
        'dddddddddddddddddddd':
        ['tree', '100644 yo\0eeeeeeeeeeeeeeeeeeee'],
        'eeeeeeeeeeeeeeeeeeee':
        ['blob', 'wuzzup!!!']
      });

    var storage = new GitStorage(mockRepo, 'upstream')
    assertSame(storage.ref, 'upstream');
    assertSame(storage.commit, 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa');
    assertEqual(storage.list(''), ['dir', 'file']);
    assertEqual(storage.list('dir/subdir/.././../dir//subdir'), ['yo']);
    assert(storage.exists('dir/subdir//'));
    assert(storage.exists('dir/subdir//yo'));
    assert(!storage.exists('file/'));
    assert(storage.isFolder('dir//'));
    assert(!storage.isFolder('file'));
    assert(!storage.isFolder('no-such'));
    assert(storage.isFile('dir/../file'));
    assert(!storage.isFile('dir'));
    assert(!storage.isFile('dir/no-such-dir/no-such-file'));
    assertSame(storage.read('file') + '', 'hello world');
    assertSame(storage.read('dir/subdir/yo') + '', 'wuzzup!!!');
    assertThrow(ValueError, function () { storage.exists('dir/../../file'); });
    assertThrow(NoSuchEntryError, function () { storage.list('no-such'); });
    assertThrow(EntryIsFileError, function () { storage.list('file'); });
    assertThrow(NoSuchEntryError, function () { storage.read('no-such'); });
    assertThrow(EntryIsFolderError, function () { storage.read('dir/subdir'); });
    assertSame(new GitStorage(mockRepo, 'HEAD').read('file') + '',
               'hi there');
    assertSame(new GitStorage(mockRepo, 'heads/upstream').read('file') + '',
               'hi there');
    assertSame(new GitStorage(mockRepo, 'master').read('file') + '',
               'hello world');
    assertThrow(ValueError,
                function () { new GitStorage(mockRepo, 'no-such'); });
    assertThrow(ValueError,
                function () { new GitStorage(mockRepo, 'bad'); });
  }
};

////////////////////////////////////////////////////////////////////////////////
// binary
////////////////////////////////////////////////////////////////////////////////

var binaryTestSuite = {
  testBinary: function () {
    assertSame(Binary(), undefined);
    var text = 'some text in russian русский текст';
    assertSame(new Binary(text) + '', text);
    var binary = new Binary(text, 'koi8');
    assert(!delete binary.length);
    binary.length = 0;
    assertSame(binary.length, text.length);
    binary = new Binary(binary, 'utf8', 'koi8');
    assertSame(binary.length, 46);
    binary = new Binary(binary, 'cp1251');
    assertSame(binary.length, text.length);
    binary = new Binary(binary);
    assertSame(binary.toString('CP1251'), text);
    assertSame(new Binary() + '', '');
    assertSame(new Binary().toString('koi8'), '');
    assertThrow(ConversionError, "new Binary('russian русский', 'ascii')");
    assertThrow(ConversionError, "new Binary('', 'no-such-charset')");
    binary = new Binary('ascii text', 'ascii');
    binary = new Binary(binary, 'utf-32', 'ascii');
    binary = new Binary(binary, 'ascii', 'utf-32');
    assertSame(binary + '', 'ascii text');
    assertSame(new Binary(3, 'x'.charCodeAt(0)) + '', 'xxx');
    var array = [];
    var asciiText = 'hello world';
    for (var i = 0; i < asciiText.length; ++i)
      array.push(asciiText.charCodeAt(i));
    assertSame(new Binary(array).toString('ascii'), asciiText);
    assertThrow(TypeError, "new Binary(new Binary(), new Binary(), 42)");
    assertSame(new Binary(new Binary('binary '),
                          new Binary('concatenation '),
                          new Binary('works!')) + '',
               'binary concatenation works!');
    assertThrow(TypeError, "new Binary(1.5)");
    assertThrow(RangeError, "new Binary(-1)");

    assertSame(
      new Binary(text, 'utf-32le').range(21 * 4, -6 * 4).toString('utf-32'),
      'русский');
    assertSame(
      new Binary(text, 'utf-16le').range(-1000, 4 * 2).toString('utf-16'),
      'some');
    assertSame(
      new Binary(text).range().range().range(21).toString(),
      'русский текст');
    assertSame(new Binary(text).range(1000).length, 0);

    binary = new Binary('Hello world    Filling works.');
    binary.range(11, 14).fill('!'.charCodeAt(0));
    assertSame(binary + '', 'Hello world!!! Filling works.');

    binary = new Binary('test test test');
    assertSame(binary.indexOf(''), 0);
    assertSame(binary.indexOf('', 5), 5);
    assertSame(binary.indexOf('', 55), 14);
    assertSame(binary.indexOf('est'), 1);
    assertSame(binary.indexOf('est', 1), 1);
    assertSame(binary.indexOf('est', 2), 6);
    assertSame(binary.indexOf('est', -5), 11);
    assertSame(binary.indexOf('no such', 2), -1);
    assertSame(binary.indexOf('est', 55), -1);
    assertSame(binary.indexOf('est', 12), -1);
    assertSame(binary.lastIndexOf(''), 14);
    assertSame(binary.lastIndexOf('', -5), 9);
    assertSame(binary.lastIndexOf('', 55), 14);
    assertSame(binary.lastIndexOf('est'), 11);
    assertSame(binary.lastIndexOf('est', 11), 11);
    assertSame(binary.lastIndexOf('est', 10), 6);
    assertSame(binary.lastIndexOf('st', -13), -1);
    assertSame(new Binary('abc').compare(new Binary('de')), -1);
    assertSame(new Binary('abc').compare(new Binary('')), 1);
    assertSame(new Binary('abc').compare(new Binary('abc')), 0);
    assertSame(new Binary('abcd').compare(new Binary('abc')), 1);
    assertSame(new Binary('abcd').compare(new Binary('abcdef')), -1);
    assertSame(new Binary().compare(new Binary('')), 0);
    assertThrow(TypeError, "new Binary().compare(42)");

    binary = new Binary(text);
    assertSame(binary.md5(), '2dc09086c2543df2ebb03147a589ae85');
    assertSame(binary.sha1(), '1c673153e2f3555eb5fd8d7670114f318fc5d5d2');
    binary = new Binary();
    assertSame(binary.md5(), 'd41d8cd98f00b204e9800998ecf8427e');
    assertSame(binary.sha1(), 'da39a3ee5e6b4b0d3255bfef95601890afd80709');
  }
};

////////////////////////////////////////////////////////////////////////////////
// proxy
////////////////////////////////////////////////////////////////////////////////

var proxyTestSuite = {
  testProxy: function () {
    assertSame(Proxy(), undefined);
    assertThrow(TypeError, "new Proxy()");
    assertThrow(TypeError, "new Proxy(42)");
    assertThrow(TypeError, "(new Proxy({})).x");
    assertEqual(
      keys(new Proxy({list: function () { return 42; }})), []);
    assertEqual(
      keys(new Proxy({list: function () { return {length: 1.5}; }})), []);
    assertEqual(
      keys(new Proxy({list: function () { return {length: -15}; }})), []);
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

        list: function () {
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
        list: throwE
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
            list: function () {
              return {get length() { throw Error(); }};
            }
          })),
      []);
    assertEqual(
      keys(
        new Proxy(
          {
            list: function () {
              var object = {length: 2, 0: 0};
              object.__defineGetter__(1, function () { throw Error(); });
              return object;
            }
          })),
      []);
  }
};

////////////////////////////////////////////////////////////////////////////////
// script
////////////////////////////////////////////////////////////////////////////////

var scriptTestSuite = {
  testScript: function () {
    assertSame((new Script('2+2')).run(), 4);
    assertSame(Script('2+2'), undefined);
    assertThrow(TypeError, "new Script()");
    assertThrow(SyntaxError, "new Script('(')");
    assertThrow(ReferenceError, "new Script('undeclarated').run()");
    assertThrow(
      SyntaxError,
      function () { new Script('new Script("(")', 'just string').run(); });
    try {
      new Script('undeclarated', 'some name', 10, 20).run();
      assert(false);
    } catch (error) {
      assert(error instanceof ReferenceError);
      assert(error.stack.indexOf('some name:11:21\n') != -1);
    }
    try {
      new Script('undeclarated', 'some name', 10).run();
      assert(false);
    } catch (error) {
      assert(error.stack.indexOf('some name:11:1\n') != -1);
    }
    try {
      new Script('undeclarated', 'some name', {}, 20).run();
      assert(false);
    } catch (error) {
      assert(error.stack.indexOf('some name:1:21\n') != -1);
    }
  }
};

////////////////////////////////////////////////////////////////////////////////
// socket
////////////////////////////////////////////////////////////////////////////////

var socketTestSuite = {
  testConnect: function () {
    assertThrow(SocketError, "connect('bad host', 'http')");
    assertThrow(SocketError, "connect('localhost', '666')");
    var socket = connect('example.com', 'http');
    assert(!socket.closed && socket.writable && socket.readable);
    var request = 'GET / HTTP/1.0\r\n\r\n';
    assertSame(socket.write(request), request.length);
    socket.shutdown('send');
    assert(!socket.writable);
    assertThrow(ValueError, function () { socket.send('yo'); });
    var response = socket.read(18);
    assertSame(response + '', 'HTTP/1.0 302 Found');
    assert(socket.read().length > 0);
    assertSame(socket.read().length, 0);
    assertSame(socket.receive(1).length, 0);
    socket.shutdown('receive');
    assert(!socket.readable);
    assertThrow(ValueError, function () { socket.receive(42); });
    socket.shutdown('both');
    assertThrow(ValueError, function () { socket.shutdown('bad'); });
    socket.close();
    assert(socket.closed);
    assertThrow(ValueError, function () { socket.send('yo'); });
    // Socket quota test. Slow to run.
//     var sockets = [];
//     for (var i = 0; i < 100; ++i)
//       sockets.push(connect('example.com', '80'));
//     assertThrow(QuotaError, "connect('example.com', '80')");
  }
};

////////////////////////////////////////////////////////////////////////////////
// http
////////////////////////////////////////////////////////////////////////////////

var httpTestSuite = {
  testHttpParser: function () {
    assertSame(HttpParser(), undefined);
    assertThrow(ValueError, "new HttpParser('bad', {})");
    assertThrow(TypeError, "new HttpParser('request', 42)");
    assertThrow(TypeError, "new HttpParser('request', {}).exec(42)");
    [
      'DELETE',
      'GET',
      'HEAD',
      'POST',
      'PUT',
      'CONNECT',
      'OPTIONS',
      'TRACE',
      'COPY',
      'LOCK',
      'MKCOL',
      'MOVE',
      'PROPFIND',
      'PROPPATCH',
      'UNLOCK'
    ].forEach(
      function (method) {
        var parsed;
        new HttpParser(
          'request',
          {onHeadersComplete: function (info) { parsed = info.method; }}).exec(
            new Binary(method + ' / HTTP/1.0\r\n\r\n'));
        assertSame(parsed, method);
      });
    var binary = new Binary('GET /some/path?a=1&b=2#fragment HTTP/1.0\r\n\r\n');
    function E() {}
    ['onMessageBegin', 'onPath', 'onHeadersComplete'].forEach(
      function (name) {
        assertThrow(
          E,
          function () {
            var handler = {};
            handler[name] = function () { throw new E(); };
            new HttpParser('request', handler).exec(binary);
          });
      });
    var Handler = function () {
      this.history = [];
    };
    Handler.prototype = {
      onMessageBegin: function () {
        this.history.push(['onMessageBegin']);
      },

      onMessageComplete: function () {
        this.history.push(['onMessageComplete']);
      },

      onHeadersComplete: function (info) {
        this.history.push(['onHeadersComplete'].concat(items(info)));
      }
    };
    [
      'onPath',
      'onURI',
      'onFragment',
      'onQueryString',
      'onHeaderField',
      'onHeaderValue',
      'onBody'
    ].forEach(
      function (name) {
        Handler.prototype[name] = function (data) {
          this.history.push([name, data + '']);
        };
      });
    var handler = new Handler();
    new HttpParser('request', {}).exec(binary);
    new HttpParser('request', handler).exec(binary);
    assertEqual(
      handler.history,
      [
        ['onMessageBegin'],
        ['onPath', '/some/path'],
        ['onQueryString', 'a=1&b=2'],
        ['onURI', '/some/path?a=1&b=2#fragment'],
        ['onFragment', 'fragment'],
        [
          'onHeadersComplete',
          ['method', 'GET'],
          ['versionMajor', 1],
          ['versionMinor', 0],
          ['shouldKeepAlive', false],
          ['upgrade', false]
        ],
        ['onMessageComplete']
      ]);
    handler = new Handler();
    var parser = new HttpParser('request', handler);
    [
      'POST /path/script.cgi HTTP/1.1\r\nContent-Type: application/',
      'x-www-form-urlencoded\r\nContent-',
      'Length: 32\r\nHost: example.com\r\n\r\nhome=Cosby&',
      'favorite+flavor=flies'
    ].forEach(
      function (s) { assertSame(parser.exec(new Binary(s)), s.length); });
    assertEqual(
      handler.history,
      [
        ['onMessageBegin'],
        ['onURI', '/path/script.cgi'],
        ['onPath', '/path/script.cgi'],
        ['onHeaderField', 'Content-Type'],
        ['onHeaderValue', 'application/'],
        ['onHeaderValue', 'x-www-form-urlencoded'],
        ['onHeaderField', 'Content-'],
        ['onHeaderField', 'Length'],
        ['onHeaderValue', '32'],
        ['onHeaderField', 'Host'],
        ['onHeaderValue', 'example.com'],
        [
          'onHeadersComplete',
          ['method', 'POST'],
          ['versionMajor', 1],
          ['versionMinor', 1],
          ['shouldKeepAlive', true],
          ['upgrade', false]
        ],
        ['onBody', 'home=Cosby&'],
        ['onBody', 'favorite+flavor=flies'],
        ['onMessageComplete']
      ]);
    assertSame(parser.exec(new Binary('bla')), 0);
    handler = new Handler();
    new HttpParser('response', handler).exec(
      new Binary('HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nyo'));
    assertEqual(
      handler.history,
      [
        ['onMessageBegin'],
        ['onHeaderField', 'Content-Length'],
        ['onHeaderValue', '2'],
        [
          'onHeadersComplete',
          ['status', 200],
          ['versionMajor', 1],
          ['versionMinor', 1],
          ['shouldKeepAlive', true],
          ['upgrade', false]
        ],
        ['onBody', 'yo'],
        ['onMessageComplete']
      ]);
  }
};

////////////////////////////////////////////////////////////////////////////////
// jsgi
////////////////////////////////////////////////////////////////////////////////

var jsgiTestSuite = {
  testJSGI: function () {
    var TestSocket = function (requestParts) {
      this.requestParts = requestParts;
      this.responseParts = [];
    };
    TestSocket.prototype = {
      receive: function () {
        return new Binary(this.requestParts.shift());
      },

      write: function (responsePart) {
        this.responseParts.push(responsePart);
      },

      get response() {
        return this.responseParts.join('');
      }
    };

    var socket = new TestSocket(
      [
        'GET /some/',
        'path/?x=',
        '42 HTTP/1.1\r\nHo',
        'st: www.',
        'example.com\r\nContent-',
        'Length: 5\r\nX-Header: one\r\nX-Header: two\r\n\r\nhell',
        'o'
      ]);

    exports.app = function (request) {
      request.headers['Set-Cookie'] = ['a=42', 'b=15'];
      return {
        status: 200,
        headers: request.headers,
        body: [
          request.method, ' ',
          request.host, ' ',
          request.pathInfo, ' ',
          request.queryString, ' ',
          request.input
        ]
      };
    };

    jsgiHandle(socket);
    assertSame(
      socket.response,
      ('HTTP/1.1 200\r\n' +
       'host: www.example.com\r\n' +
       'content-length: 5\r\n' +
       'x-header: one,two\r\n' +
       'Set-Cookie: a=42\r\n' +
       'Set-Cookie: b=15\r\n' +
       '\r\n' +
       'GET www.example.com /some/path/ x=42 hello'));
  }
};

////////////////////////////////////////////////////////////////////////////////
// Entry points
////////////////////////////////////////////////////////////////////////////////

test = function () {
  return runTestSuites(
    [
      coreTestSuite,
      dbTestSuite1,
      dbTestSuite2,
      fsTestSuite,
      gitTestSuite,
      binaryTestSuite,
      proxyTestSuite,
      scriptTestSuite,
      socketTestSuite,
      httpTestSuite,
      jsgiTestSuite
    ]);
};


exports.handle = function (socket) {
  socket.write(eval(socket.read() + ''));
};


throw42 = function () {
  throw 42;
};
