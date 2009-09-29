
// (c) 2009 by Anton Korenyushkin

(function ()
{
  //////////////////////////////////////////////////////////////////////////////
  // Errors
  //////////////////////////////////////////////////////////////////////////////

  Error.stackTraceLimit = 1000;


  function defineErrorClass(name, parent) {
    var fullName = name + 'Error';
    var result = function (message) {
      Error.captureStackTrace(this);
      this.message = message + '';
    };
    result.__name__ = result.prototype.name = 'ak.' + fullName;
    result.prototype.__proto__ = parent.prototype;
    ak[fullName] = result;
    return result;
  }


  defineErrorClass('Core', Error);

  defineErrorClass('Usage', ak.CoreError);
  defineErrorClass('DB', ak.CoreError);
  defineErrorClass('FS', ak.CoreError);
  defineErrorClass('App', ak.CoreError);


  ak._setObjectProp(
    ak, '_errors', 7,
    [
      TypeError,

      ak.CoreError,

      ak.UsageError,
      ak.DBError,
      ak.FSError,
      ak.AppError,

      defineErrorClass('DBQuota', ak.DBError),
      defineErrorClass('RelationExists', ak.DBError),
      defineErrorClass('NoSuchRelation', ak.DBError),
      defineErrorClass('RelationDependency', ak.DBError),
      defineErrorClass('ConstraintViolation', ak.DBError),
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
      defineErrorClass('CyclicInclude', ak.FSError),
      defineErrorClass('Conversion', ak.FSError),

      defineErrorClass('AppException', ak.AppError),
      defineErrorClass('NoSuchApp', ak.AppError),
      defineErrorClass('InvalidAppName', ak.AppError),
      defineErrorClass('SelfRequest', ak.AppError),
      defineErrorClass('RequestTimedOut', ak.AppError)
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


  ak.path = undefined;


  var baseApp = '';
  var baseDir = '';
  var currDir = '';
  var includeStack = [];
  var includeResults = {};


  function doInclude(app, path) {
    path = canonicalize(path);
    var identifier = app + '/' + path;
    if (identifier in includeResults)
      return includeResults[identifier];
    for (var i = 0; i < includeStack.length; ++i)
      if (includeStack[i] == identifier)
        throw new ak.CyclicIncludeError(
          'Recursive include of file "' + path + '"' +
          (app ? ' of ' + app + ' app': ''));

    var oldCurrDir = currDir;
    var oldBaseDir = baseDir;
    var oldBaseApp = baseApp;
    var oldPath    = ak.path;

    var idx = path.lastIndexOf('/');
    var newCurrDir = idx == -1 ? '' : path.substring(0, idx);
    var newBaseDir = arguments.length > 1 ? newCurrDir : baseDir;

    includeStack.push(identifier);
    baseApp = app;
    currDir = newCurrDir;
    baseDir = newBaseDir;
    ak.path = path;
    try {
      var script = (app
                    ? ak._compile(ak._readCode(app, path), app + ':' + path)
                    : ak._compile(ak._readCode(path), path));
      var result = script._run();
      includeResults[identifier] = result;
      return result;
    } finally {
      ak.path = oldPath;
      baseApp = oldBaseApp;
      currDir = oldCurrDir;
      baseDir = oldBaseDir;
      includeStack.pop();
    }
  }


  ak.include = function (/* [libPath,] filePath */) {
    var app, path;
    switch (arguments.length) {
    case 0:
      throw new ak.UsageError('At least one argument required');
    case 1:
      var filePath = arguments[0] + '';
      app = baseApp;
      path = filePath[0] == '/' ? baseDir + filePath : currDir + '/' + filePath;
      break;
    default:
      var libPath = arguments[0] + '';
      var idx = libPath.indexOf('/');
      if (idx == -1) {
        app = libPath;
        path = arguments[1] + '';
      } else {
        app = libPath.substring(0, idx);
        path = libPath.substring(idx + 1) + '/' + arguments[1];
      }
      if (app == ak._appName)
        app = '';
    }
    return doInclude(app, path);
  };


  ak.use = function (libPath) {
    return ak.include(libPath, '__init__.js');
  };

})();
