
// (c) 2009-2010 by Anton Korenyushkin

(function ()
{
  //////////////////////////////////////////////////////////////////////////////
  // Errors
  //////////////////////////////////////////////////////////////////////////////

  var global = this;


  Error.stackTraceLimit = 1000;


  function defineErrorClass(name, parent/* = Error */) {
    var fullName = name + 'Error';
    var result = function (message) {
      if (!(this instanceof arguments.callee)) {
        var self = {__proto__: arguments.callee.prototype};
        arguments.callee.apply(self, arguments);
        return self;
      }
      Error.captureStackTrace(this);
      this.message = message + '';
      return undefined;
    };
    result.prototype.name = fullName;
    result.prototype.__proto__ = (parent || Error).prototype;
    global[fullName] = result;
    return result;
  }


  defineErrorClass('DB');
  defineErrorClass('FS');


  errors = [
    TypeError,
    RangeError,

    defineErrorClass('Value'),
    defineErrorClass('Usage'),
    defineErrorClass('NotImplemented'),

    defineErrorClass('NoSuchApp'),
    defineErrorClass('NoSuchUser'),
    defineErrorClass('Conversion'),
    defineErrorClass('Socket'),
    defineErrorClass('Quota'),

    DBError,
    defineErrorClass('RelVarExists', DBError),
    defineErrorClass('NoSuchRelVar', DBError),
    defineErrorClass('Constraint', DBError),
    defineErrorClass('Query', DBError),
    defineErrorClass('AttrExists', DBError),
    defineErrorClass('NoSuchAttr', DBError),
    defineErrorClass('AttrValueRequired', DBError),
    defineErrorClass('RelVarDependency', DBError),

    FSError,
    defineErrorClass('Path', FSError),
    defineErrorClass('EntryExists', FSError),
    defineErrorClass('NoSuchEntry', FSError),
    defineErrorClass('EntryIsDir', FSError),
    defineErrorClass('EntryIsNotDir', FSError),
    defineErrorClass('FileIsReadOnly', FSError)
  ];

  //////////////////////////////////////////////////////////////////////////////
  // require
  //////////////////////////////////////////////////////////////////////////////

  function parsePath(path, dir) {
    var parts = path.split('/');
    var result = path[0] == '.' ? dir.slice() : [];
    for (var i = 0; i < parts.length; ++i) {
      switch (parts[i]) {
      case '':
      case '.':
        break;
      case '..':
        if (!result.length)
          throw PathError('Invalid code path: ' + path);
        result.pop();
        break;
      default:
        result.push(parts[i]);
      }
    }
    return result;
  }


  var cache = {};
  var main;


  function makeRequire(baseDir) {
    return function (path) {
      var loc = parsePath(path, baseDir);
      var id = loc.join('/');
      if (cache.hasOwnProperty(id))
        return cache[id];
      var absPath = id + '.js';
      var code = core.readCode(absPath);
      var func = new script.Script(
        '(function (require, exports, module) {\n' + code + '\n})',
        absPath, -1)._run();
      var require = makeRequire(loc.slice(0, loc.length - 1));
      var exports = cache[id] = {};
      var module = {exports: exports, id: id};
      var oldMain = main;
      main = main || module;
      core.set(require, 'main', 5, main);
      try {
        func(require, exports, module);
      } catch (error) {
        delete cache[id];
        main = oldMain;
        throw error;
      }
      return exports;
    };
  }


  require = makeRequire([]);

})();
