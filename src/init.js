// (c) 2009-2010 by Anton Korenyushkin

(function (basis, errorClasses)
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


  errorClasses.push(
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
    subclassError('fs', 'EntryIsFolderError', FSError),
    subclassError('fs', 'EntryIsFileError', FSError),

    subclassError('binary', 'ConversionError'),

    subclassError('socket', 'SocketError'));


  function readSafely(storage, path) {
    try {
      if (storage.read)
        return storage.read(path);
      var file = storage.open(path);
    } catch (error) {
      if (error instanceof FSError)
        return null;
      else
        throw error;
    }
    try {
      return file.read();
    } finally {
      file.close();
    }
  }


  function Place(storage, prefix) {
    this.storage = storage;
    this.prefix = prefix;
    this.cache = {};
    var manifest = readSafely(storage, 'manifest.json');
    if (manifest) {
      try {
        this.deps = JSON.parse(manifest).deps;
      } catch (error) {
        throw RequireError('Failed to parse manifest.json: ' + error.message);
      }
    } else {
      this.deps = {};
    }
  }


  var defaultPlace = new Place(basis.fs.lib, 'default:');
  var mainPlace = new Place(basis.fs.code, '');
  var main = {id: 'main', exports: {}};
  var gitPlaces = {};
  var defaultRequire;
  var GitStorage;


  function doRequire(place, id, dir) {
    if (place.cache.hasOwnProperty(id))
      return place.cache[id];
    var path = id + '.js';
    var code = readSafely(place.storage, path);
    if (!code)
      return null;
    var func = new basis.script.Script(
      '(function (require, exports, module) {\n' + code + '\n})',
      place.prefix + path, -1).run();
    var require = makeRequire(place, dir);
    var exports = place.cache[id] =
      place === defaultPlace && basis.hasOwnProperty(id) ? basis[id] : {};
    var module = main.exports ? {id: id} : main;
    module.exports = exports;
    basis.core.set(require, 'main', 5, main);
    try {
      func(require, exports, module);
    } catch (error) {
      delete place.cache[id];
      delete module.exports;
      throw error;
    }
    return exports;
  }


  function doLibRequire(place, libName, id, dir) {
    if (libName == 'default')
      return doRequire(defaultPlace, id, dir);
    if (!place.deps.hasOwnProperty(libName))
      return undefined;
    var ref = place.deps[libName];
    var refs;
    if (gitPlaces.hasOwnProperty(libName)) {
      refs = gitPlaces[libName];
      if (refs.hasOwnProperty(ref))
        return doRequire(refs[ref], id, dir);
    }
    GitStorage = GitStorage || defaultRequire('git').GitStorage;
    var libPlace = new Place(new GitStorage(libName, ref),
                             libName + ':' + ref + ':');
    if (!refs)
      gitPlaces[libName] = refs = {};
    refs[ref] = libPlace;
    return doRequire(libPlace, id, dir);
  }


  function makeRequire(place, dir) {
    return function () {
      if (!arguments.length)
        throw TypeError('At least one argument required');
      var libName;
      var rawId;
      if (arguments.length == 1) {
        rawId = arguments[0];
      } else {
        libName = arguments[0];
        rawId = arguments[1];
      }
      var parts = rawId.split('/');
      var loc = parts[0] == '.' ? dir.slice() : [];
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
      loc.pop();
      var result;
      if (libName) {
        result = doLibRequire(place, libName, id, loc);
        if (result === undefined)
          throw RequireError('No such lib: ' + libName);
        if (result === null)
          throw RequireError('Module not found: ' + libName + ':' + rawId);
      } else {
        result = doRequire(place, id, loc);
        if (!result && parts.length == 1)
          result = doLibRequire(place, rawId, 'index', []);
        if (!result && parts[0] != '.' && place !== defaultPlace)
          result = doLibRequire(place, 'default', id, loc);
        if (!result)
          throw RequireError('Module not found: ' + rawId);
      }
      return result;
    };
  }


  require = makeRequire(mainPlace, []);
  basis.core.set(require, 'main', 5, main);


  defaultRequire = makeRequire(defaultPlace, []);
  defaultRequire('binary');
  defaultRequire('socket');
  delete main.exports;
});
