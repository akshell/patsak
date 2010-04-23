
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
        return ak._construct(arguments.callee, arguments);
      Error.captureStackTrace(this);
      this.message = message + '';
      return undefined;
    };
    result.__name__ = result.prototype.name = 'ak.' + fullName;
    result.prototype.__proto__ = parent.prototype;
    ak[fullName] = result;
    return result;
  }


  defineErrorClass('Base', Error);

  defineErrorClass('Core', ak.BaseError);
  defineErrorClass('Usage', ak.BaseError);

  defineErrorClass('DB', ak.CoreError);
  defineErrorClass('FS', ak.CoreError);
  defineErrorClass('AppRequest', ak.CoreError);
  defineErrorClass('HostRequest', ak.CoreError);
  defineErrorClass('Metadata', ak.CoreError);


  ak._set(
    ak, '_errors', 7,
    [
      TypeError,

      ak.BaseError,

      ak.CoreError,
      ak.UsageError,

      ak.DBError,
      ak.FSError,
      ak.AppRequestError,
      ak.HostRequestError,
      ak.MetadataError,

      defineErrorClass('DBQuota', ak.DBError),
      defineErrorClass('RelVarExists', ak.DBError),
      defineErrorClass('NoSuchRelVar', ak.DBError),
      defineErrorClass('RelVarDependency', ak.DBError),
      defineErrorClass('Constraint', ak.DBError),
      defineErrorClass('Field', ak.DBError),
      defineErrorClass('Query', ak.DBError),

      defineErrorClass('FSQuota', ak.FSError),
      defineErrorClass('Path', ak.FSError),
      defineErrorClass('EntryExists', ak.FSError),
      defineErrorClass('NoSuchEntry', ak.FSError),
      defineErrorClass('EntryIsDir', ak.FSError),
      defineErrorClass('EntryIsNotDir', ak.FSError),
      defineErrorClass('DirIsNotEmpty', ak.FSError),
      defineErrorClass('TempFileRemoved', ak.FSError),
      defineErrorClass('Conversion', ak.FSError),

      defineErrorClass('ProcessingFailed', ak.AppRequestError),
      defineErrorClass('TimedOut', ak.AppRequestError),

      defineErrorClass('NoSuchApp', ak.MetadataError),
      defineErrorClass('NoSuchUser', ak.MetadataError)
    ]);

  //////////////////////////////////////////////////////////////////////////////
  // include and use
  //////////////////////////////////////////////////////////////////////////////

  function canonicalize(path) {
    var bits = path.split('/');
    var resultBits = [];
    function checkNonEmpty() {
      if (!resultBits.length)
        throw new ak.PathError('Code path "' + path + '" is illegal');
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


  ak.include = function (/* [app,] path */) {
    if (!arguments.length)
      throw ak.UsageError('At least one argument required');
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
        throw ak.UsageError(
          'Recursive include of file "' + path + '"' +
          (app ? ' of ' + app + ' app': ''));

    var oldCurrApp = currApp;
    var oldCurrDir = currDir;
    var oldPath = ak.include.path;

    var idx = path.lastIndexOf('/');
    currDir = idx == -1 ? '' : path.substring(0, idx + 1);
    currApp = app;
    ak.include.path = path;
    includeStack.push(identifier);

    try {
      var script = (app
                    ? new ak.Script(ak._readCode(app, path), app + ':' + path)
                    : new ak.Script(ak._readCode(path), path));
      var result = script._run();
      includeResults[identifier] = result;
      return result;
    } finally {
      currApp = oldCurrApp;
      currDir = oldCurrDir;
      ak.include.path = oldPath;
      includeStack.pop();
    }
  };


  ak.use = function (app, path/* = '' */) {
    return ak.include(app, (path ? path + '/' : '') + '__init__.js');
  };

})();
