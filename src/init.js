
// (c) 2009-2010 by Anton Korenyushkin

(function (basis)
{
  Error.stackTraceLimit = 1000;


  function subclassError(moduleName, className, parent/* = Error */) {
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
    result.prototype.name = className;
    result.prototype.__proto__ = (parent || Error).prototype;
    basis[moduleName][className] = result;
    return result;
  }


  var RequireError = subclassError('core', 'RequireError');
  var DBError = subclassError('db', 'DBError');
  var FSError = subclassError('fs', 'FSError');


  var codeCache = {};
  var libCache = {};
  var main;


  function openSafely(storage, path) {
    try {
      return storage.open(path);
    } catch (error) {
      if (error instanceof FSError)
        return null;
      else
        throw error;
    }
  }


  function makeRequire(storage, dir) {
    return function (rawId) {
      var parts = rawId.split('/');
      var isRelative = parts[0] == '.';
      var loc = isRelative ? dir.slice() : [];
      for (var i = 0; i < parts.length; ++i) {
        switch (parts[i]) {
        case '':
        case '.':
          break;
        case '..':
          if (!loc.length)
            throw RequireError('Invalid require path: ' + rawId);
          loc.pop();
          break;
        default:
          loc.push(parts[i]);
        }
      }
      var id = loc.join('/');
      var path = id + '.js';
      var isLib = storage === basis.fs.lib;
      var file;
      if (!isLib) {
        if (codeCache.hasOwnProperty(id))
          return codeCache[id];
        file = openSafely(storage, path);
        if (!file && !isRelative)
          isLib = true;
      }
      if (isLib) {
        if (libCache.hasOwnProperty(id))
          return libCache[id];
        file = openSafely(basis.fs.lib, path);
      }
      if (!file)
        throw RequireError('Module not found: ' + rawId);
      try {
        var code = file.read();
      } finally {
        file.close();
      }
      var func = new basis.script.Script(
        '(function (require, exports, module) {\n' + code + '\n})',
        path, -1).run();
      var require = makeRequire(isLib ? basis.fs.lib : storage,
                                loc.slice(0, loc.length - 1));
      var cache = isLib ? libCache : codeCache;
      var exports = cache[id] = (isLib && basis.hasOwnProperty(id)
                                 ? basis[id]
                                 : {});
      var module = {exports: exports, id: id};
      var oldMain = main;
      main = main || module;
      basis.core.set(require, 'main', 5, main);
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


  require = makeRequire(basis.fs.code, []);


  return [
    TypeError,
    RangeError,

    subclassError('core', 'ValueError'),
    subclassError('core', 'NotImplementedError'),
    subclassError('core', 'QuotaError'),

    DBError,
    subclassError('db', 'RelVarExistsError', DBError),
    subclassError('db', 'NoSuchRelVarError', DBError),
    subclassError('db', 'AttrExistsError', DBError),
    subclassError('db', 'NoSuchAttrError', DBError),
    subclassError('db', 'ConstraintError', DBError),
    subclassError('db', 'QueryError', DBError),
    subclassError('db', 'DependencyError', DBError),

    FSError,
    subclassError('fs', 'EntryExistsError', FSError),
    subclassError('fs', 'NoSuchEntryError', FSError),
    subclassError('fs', 'EntryIsDirError', FSError),
    subclassError('fs', 'EntryIsFileError', FSError),

    subclassError('binary', 'ConversionError'),

    subclassError('socket', 'SocketError')
  ];
});
