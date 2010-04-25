
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
  defineErrorClass('HostRequest', _core.CoreError);
  defineErrorClass('Metadata', _core.CoreError);


  _core.errors = [
    TypeError,

    _core.BaseError,

    _core.CoreError,
    _core.UsageError,

    _core.DBError,
    _core.FSError,
    _core.AppRequestError,
    _core.HostRequestError,
    _core.MetadataError,

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
    defineErrorClass('Conversion', _core.FSError),

    defineErrorClass('ProcessingFailed', _core.AppRequestError),
    defineErrorClass('TimedOut', _core.AppRequestError),

    defineErrorClass('NoSuchApp', _core.MetadataError),
    defineErrorClass('NoSuchUser', _core.MetadataError)
  ];

  //////////////////////////////////////////////////////////////////////////////
  // include and use
  //////////////////////////////////////////////////////////////////////////////

  function canonicalize(path) {
    var bits = path.split('/');
    var resultBits = [];
    function checkNonEmpty() {
      if (!resultBits.length)
        throw new _core.PathError('Code path "' + path + '" is illegal');
    }
    for (var i = 0; i < bits.length; ++i) {
      switch (bits[i]) {
      case '':
      case '.':
        break;
      case '..':
        checkNonEmpty();
        resultBits.pop();
        break;
      default:
        resultBits.push(bits[i]);
      };
    }
    checkNonEmpty();
    return resultBits.join('/');
  }


  var currApp = '';
  var currDir = '';
  var includeStack = [];
  var includeResults = {};


  _core.include = function (/* [app,] path */) {
    if (!arguments.length)
      throw _core.UsageError('At least one argument required');
    var app, path;
    if (arguments.length > 1) {
      app = arguments[0];
      path = arguments[1];
    } else {
      app = currApp;
      path = arguments[0];
      if (path[0] != '/')
        path = currDir + path;
    }
    path = canonicalize(path);
    var identifier = app + ':' + path;
    if (includeResults.hasOwnProperty(identifier))
      return includeResults[identifier];
    for (var i = 0; i < includeStack.length; ++i)
      if (includeStack[i] == identifier)
        throw _core.UsageError(
          'Recursive include of file "' + path + '"' +
          (app ? ' of ' + app + ' app': ''));

    var oldCurrApp = currApp;
    var oldCurrDir = currDir;
    var oldPath = _core.include.path;

    var idx = path.lastIndexOf('/');
    currDir = idx == -1 ? '' : path.substring(0, idx + 1);
    currApp = app;
    _core.include.path = path;
    includeStack.push(identifier);

    try {
      var script = (
        app
        ? new _core.Script(_core.readCode(app, path), app + ':' + path)
        : new _core.Script(_core.readCode(path), path));
      var result = script._run();
      includeResults[identifier] = result;
      return result;
    } finally {
      currApp = oldCurrApp;
      currDir = oldCurrDir;
      _core.include.path = oldPath;
      includeStack.pop();
    }
  };


  _core.use = function (app, path/* = '' */) {
    return _core.include(app, (path ? path + '/' : '') + '__init__.js');
  };

})();
