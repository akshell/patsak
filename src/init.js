
// (c) 2009-2010 by Anton Korenyushkin

(function ()
{
  //////////////////////////////////////////////////////////////////////////////
  // Errors
  //////////////////////////////////////////////////////////////////////////////

  Error.stackTraceLimit = 1000;


  function defineErrorClass(name, parent/* = Error */) {
    var fullName = name + 'Error';
    var result = function (message) {
      if (!(this instanceof arguments.callee))
        return _core.construct(arguments.callee,
                               Array.prototype.slice.call(arguments));
      Error.captureStackTrace(this);
      this.message = message + '';
      return undefined;
    };
    result.prototype.name = fullName;
    result.prototype.__proto__ = (parent || Error).prototype;
    _core[fullName] = result;
    return result;
  }


  defineErrorClass('DB');
  defineErrorClass('FS');


  _core.errors = [
    TypeError,
    RangeError,

    defineErrorClass('Value'),
    defineErrorClass('Usage'),
    defineErrorClass('NotImplemented'),

    defineErrorClass('RequestHost'),
    defineErrorClass('NoSuchApp'),
    defineErrorClass('NoSuchUser'),
    defineErrorClass('Conversion'),

    _core.DBError,
    defineErrorClass('RelVarExists', _core.DBError),
    defineErrorClass('NoSuchRelVar', _core.DBError),
    defineErrorClass('Constraint', _core.DBError),
    defineErrorClass('Query', _core.DBError),
    defineErrorClass('AttrExists', _core.DBError),
    defineErrorClass('NoSuchAttr', _core.DBError),
    defineErrorClass('AttrValueRequired', _core.DBError),
    defineErrorClass('RelVarDependency', _core.DBError),
    defineErrorClass('DBQuota', _core.DBError),

    _core.FSError,
    defineErrorClass('FSQuota', _core.FSError),
    defineErrorClass('Path', _core.FSError),
    defineErrorClass('EntryExists', _core.FSError),
    defineErrorClass('NoSuchEntry', _core.FSError),
    defineErrorClass('EntryIsDir', _core.FSError),
    defineErrorClass('EntryIsNotDir', _core.FSError),
    defineErrorClass('FileIsReadOnly', _core.FSError)
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
          throw _core.PathError('Illegal code path: ' + path);
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
        throw _core.UsageError('At least 1 argument required');
      if (arguments.length == 1) {
        app = baseApp;
        version = baseVersion;
        loc = parsePath(arguments[0], baseDir);
      } else {
        app = arguments[0];
        if (!_core.spot && app == _core.app)
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
      var code = app ? _core.readCode(app, path) : _core.readCode(path);
      var func = new _core.Script(
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
        module.app = _core.app;
        if (_core.spot) {
          module.owner = _core.owner;
          module.spot = _core.spot;
        }
      }
      var oldMain = main;
      main = main || module;
      _core.set(require, 'main', 5, main);
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
