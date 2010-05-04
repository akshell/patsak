
// (c) 2009-2010 by Anton Korenyushkin

(function ()
{
  //////////////////////////////////////////////////////////////////////////////
  // Errors
  //////////////////////////////////////////////////////////////////////////////

  Error.stackTraceLimit = 1000;


  function defineErrorClass(name, parent) {
    var fullName = name + 'Error';
    var result = function (message) {
      if (!(this instanceof arguments.callee))
        return _core.construct(arguments.callee, arguments);
      Error.captureStackTrace(this);
      this.message = message + '';
      return undefined;
    };
    result.__name__ = result.prototype.name = fullName;
    result.prototype.__proto__ = parent.prototype;
    _core[fullName] = result;
    return result;
  }


  defineErrorClass('Base', Error);

  defineErrorClass('Core', _core.BaseError);
  defineErrorClass('Usage', _core.BaseError);

  defineErrorClass('DB', _core.CoreError);
  defineErrorClass('FS', _core.CoreError);
  defineErrorClass('AppRequest', _core.CoreError);


  _core.errors = [
    TypeError,
    RangeError,

    _core.BaseError,

    _core.CoreError,
    _core.UsageError,

    _core.DBError,
    _core.FSError,
    _core.AppRequestError,

    defineErrorClass('HostRequest', _core.CoreError),
    defineErrorClass('NoSuchApp', _core.CoreError),
    defineErrorClass('NoSuchUser', _core.CoreError),
    defineErrorClass('Conversion', _core.CoreError),

    defineErrorClass('DBQuota', _core.DBError),
    defineErrorClass('RelVarExists', _core.DBError),
    defineErrorClass('NoSuchRelVar', _core.DBError),
    defineErrorClass('RelVarDependency', _core.DBError),
    defineErrorClass('Constraint', _core.DBError),
    defineErrorClass('Field', _core.DBError),
    defineErrorClass('Query', _core.DBError),

    defineErrorClass('FSQuota', _core.FSError),
    defineErrorClass('Path', _core.FSError),
    defineErrorClass('EntryExists', _core.FSError),
    defineErrorClass('NoSuchEntry', _core.FSError),
    defineErrorClass('EntryIsDir', _core.FSError),
    defineErrorClass('EntryIsNotDir', _core.FSError),
    defineErrorClass('DirIsNotEmpty', _core.FSError),
    defineErrorClass('TempFileRemoved', _core.FSError),

    defineErrorClass('ProcessingFailed', _core.AppRequestError),
    defineErrorClass('TimedOut', _core.AppRequestError)
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
      var module = {id: loc.join('/'), exports: exports};
      if (version.length)
        module.version = version.join('/');
      if (app) {
        module.app = app;
      } else {
        module.app = _core.app;
        if (_core.spot) {
          module.owner = _core.owner;
          module.spot = _core.spot;
        }
      }
      if (!main)
        main = module;
      _core.set(require, 'main', 5, main);
      func(require, exports, module);
      return exports;
    };
  }


  require = makeRequire('', [], []);

})();
