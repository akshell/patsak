
// (c) 2009 by Anton Korenyushkin

(function ()
{
  var baseAppName = '';
  var baseDir = '';
  var currDir = '';


  function handlePathes(/* [libPath,] filePath */) {
    switch (arguments.length) {
    case 0:
      throw new Error('At least one argument required');
    case 1:
      var filePath = arguments[0];
      return [baseAppName,
              (filePath
               ? (filePath[0] == '/'
                  ? baseDir + filePath
                  : currDir + '/' + filePath)
               : '')];
    default:
      var libPath = arguments[0] + '';
      var idx = libPath.indexOf('/');
      var appName, fullPath;
      if (idx == -1) {
        appName = libPath;
        fullPath = arguments[1] + '';
      } else {
        appName = libPath.substring(0, idx);
        fullPath = libPath.substring(idx + 1) + '/' + arguments[1];
      }
      if (appName == ak._appName)
        appName = '';
      return [appName, fullPath];
    }
  }


  ak.readCode = function (/* [libPath,] filePath */) {
    var ret = handlePathes.apply(this, arguments);
    var appName = ret[0], fullPath = ret[1];
    return appName ? ak._readCode(appName, fullPath) : ak._readCode(fullPath);
  };


  function canonicalize(path) {
    var bits = path.split('/');
    var resultBits = [];
    function checkNonEmpty() {
      if (!resultBits.length)
        throw new Error('Code path "' + path + '" is illegal');
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


  var includeStack = [];
  var includeResults = {};


  ak.include = function (/* [libPath,] filePath */) {
    var ret = handlePathes.apply(this, arguments);
    var appName = ret[0], fullPath = canonicalize(ret[1]);

    var identifier = appName + '/' + fullPath;
    if (identifier in includeResults)
      return includeResults[identifier];
    for (var i = 0; i < includeStack.length; ++i)
      if (includeStack[i] == identifier)
        throw new Error('Recursive including of file "' + fullPath + '"' +
                        (appName ? ' of ' + appName + ' app': ''));

    var oldCurrDir = currDir;
    var oldBaseDir = baseDir;
    var oldBaseAppName = baseAppName;
    
    var idx = fullPath.lastIndexOf('/');
    var newCurrDir = idx == -1 ? '' : fullPath.substring(0, idx);
    var newBaseDir = arguments.length > 1 ? newCurrDir : baseDir;

    includeStack.push(identifier);
    baseAppName = appName;
    currDir = newCurrDir;
    baseDir = newBaseDir;
    try {
      var script = (appName
                    ? ak._compile(ak._readCode(appName, fullPath),
                                  appName + ':' + fullPath)
                    : ak._compile(ak._readCode(fullPath),
                                  fullPath));
      var result = script._run();
      includeResults[identifier] = result;
      return result;
    } finally {
      baseAppName = oldBaseAppName;
      currDir = oldCurrDir;
      baseDir = oldBaseDir;
      includeStack.pop();
    }
  };


  ak.use = function (libPath) {
    return ak.include(libPath, '__init__.js');
  };

})();
