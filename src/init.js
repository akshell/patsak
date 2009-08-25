
// (c) 2009 by Anton Korenyushkin

(function ()
{
  var baseAppName = '';
  var basePath = '';


  function handlePathes(/* [libPath,] filePath */) {
    switch (arguments.length) {
    case 0:
      throw new Error('At least one argument required');
    case 1:
      var filePath = arguments[0];
      return [baseAppName,
              filePath[0] == '/' ? filePath + '' : basePath + '/' + filePath];
    default:
      var libPath = arguments[0] + '';
      var idx = libPath.indexOf('/');
      if (idx == -1)
        return [libPath, arguments[1] + ''];
      return [libPath.substring(0, idx),
              libPath.substring(idx + 1) + '/' + arguments[1]];
    }
  }


  ak.readCode = function (/* [libPath,] filePath */) {
    var ret = handlePathes.apply(this, arguments);
    var appName = ret[0], fullPath = ret[1];
    return appName ? ak._readCode(appName, fullPath) : ak._readCode(fullPath);
  };


  var includeStack = [];


  ak.include = function (/* [libPath,] filePath */) {
    var ret = handlePathes.apply(this, arguments);
    var appName = ret[0], fullPath = ret[1];

    var identifier = appName + ':' + fullPath;
    for (var i = 0; i < includeStack.length; ++i)
      if (includeStack[i] == identifier)
        throw new Error('Recursive including of file "' + fullPath + '"' +
                        (appName ? ' of ' + appName + ' app': ''));

    var oldBasePath = basePath;
    var idx = fullPath.lastIndexOf('/');
    var newBasePath = idx == -1 ? '' : fullPath.substring(0, idx);
    var oldBaseAppName = baseAppName;

    includeStack.push(identifier);
    baseAppName = appName;
    basePath = newBasePath;
    try {
      var script = (appName
                    ? ak._compile(ak._readCode(appName, fullPath),
                                  appName + ':' + fullPath)
                    : ak._compile(ak._readCode(fullPath),
                                  fullPath));
      return script._run();
    } finally {
      baseAppName = oldBaseAppName;
      basePath = oldBasePath;
      includeStack.pop();
    }
  };
})();
