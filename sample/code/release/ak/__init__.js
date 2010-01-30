
// (c) 2009-2010 by Anton Korenyushkin

(function ()
{
  ak.include('MochiKit.js');


  ak.NONE = 0;
  ak.READ_ONLY   = 1 << 0;
  ak.DONT_ENUM   = 1 << 1;
  ak.DONT_DELETE = 1 << 2;


  ak.fs.remove = function (path) {
    if (ak.fs._isDir(path)) {
      var children = ak.fs._list(path);
      for (var i = 0; i < children.length; ++i)
        arguments.callee(path + '/' + children[i]);
    }
    ak.fs._remove(path);
  };


  ak._set(
    ak.Data.prototype,
    'toString',
    ak.DONT_ENUM,
    function (encoding) {
      return this._toString(encoding || 'UTF-8');
    });
})();
