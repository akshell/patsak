
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

  function parsePath(path, dir/* = [] */) {
    var parts = path.split('/');
    var result = dir && path[0] == '.' ? dir.slice() : [];
    for (var i = 0; i < parts.length; ++i) {
      switch (parts[i]) {
      case '':
      case '.':
        break;
      case '..':
        if (!result.length)
          throw PathError('Illegal code path: ' + path);
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


  function makeRequire(baseApp, baseVersion, baseDir) {
    return function () {
      var app, version, dir, loc;
      if (!arguments.length)
        throw UsageError('At least 1 argument required');
      if (arguments.length == 1) {
        app = baseApp;
        version = baseVersion;
        loc = parsePath(arguments[0], baseDir);
      } else {
        app = arguments[0];
        if (!global.spot && app == global.app)
          app = '';
        version = parsePath(arguments[1]);
        if (arguments.length == 2)
          loc = ['index'];
        else
          loc = parsePath(arguments[2]);
      }
      var key = [app].concat(version, loc).join('/');
      if (cache.hasOwnProperty(key))
        return cache[key];
      var path = version.concat(loc).join('/') + '.js';
      var code = app ? core.readCode(app, path) : core.readCode(path);
      var func = new script.Script(
        '(function (require, exports, module) {\n' + code + '\n})',
        (app && app + ':') + path, -1)._run();
      var require = makeRequire(app, version, loc.slice(0, loc.length - 1));
      var exports = cache[key] = {};
      var module = {
        exports: exports,
        id: loc.join('/'),
        version: version.join('/')
      };
      if (app) {
        module.app = app;
      } else {
        module.app = global.app;
        if (global.spot) {
          module.owner = global.owner;
          module.spot = global.spot;
        }
      }
      var oldMain = main;
      main = main || module;
      core.set(require, 'main', 5, main);
      try {
        func(require, exports, module);
      } catch (error) {
        delete cache[key];
        main = oldMain;
        throw error;
      }
      return exports;
    };
  }


  require = makeRequire('', [], []);

})();
