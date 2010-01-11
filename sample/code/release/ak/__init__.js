
// (c) 2009-2010 by Anton Korenyushkin

(function ()
{
  ak.include('MochiKit.js');


  registerComparator('TypeComparator',
                     function (lhs, rhs) {
                       return lhs instanceof ak.Type && rhs instanceof ak.Type;
                     },
                     function (lhs, rhs) {
                       return lhs === rhs;
                     });

  
  ak.Selection.prototype.__proto__ = ak.Rel.prototype;


  ak.NONE = 0;
  ak.READ_ONLY   = 1 << 0;
  ak.DONT_ENUM   = 1 << 1;
  ak.DONT_DELETE = 1 << 2;


  ak._setObjectProp(Object.prototype,
                    'setProp',
                    ak.DONT_ENUM,
                    function (name, attrs, value) {
                      return ak._setObjectProp(this, name, attrs, value);
                    });


  function field(field_name) {
    if (arguments.length != 1)
      throw Error('field() requires exactly one argument');
    if (typeof(field_name) == 'object' && field_name.length)
      throw TypeError('field() argument must not be array-like');
    var query = ak.Rel.prototype._only.call(this, field_name);
    var result = [];
    for (var i = 0; i < query.length; ++i)
      result.push(query[i][field_name]);
    return result;
  }


  ak.Rel.prototype.setProp('field', ak.DONT_ENUM, field);


  function makeRelVarDelegation(func_name)
  {
    ak.RelVar.prototype[func_name] = function () {
      return ak.Selection.prototype[func_name].apply(this._all(), arguments);
    };
  }

  makeRelVarDelegation('_where');
  makeRelVarDelegation('_only');
  makeRelVarDelegation('_by');
  makeRelVarDelegation('_subrel');
  makeRelVarDelegation('_count');
  makeRelVarDelegation('field');
  makeRelVarDelegation('_update');
  makeRelVarDelegation('_delete');


  ak.fs.remove = function (path) {
    if (ak.fs._isDir(path)) {
      var children = ak.fs._list(path);
      for (var i = 0; i < children.length; ++i)
        arguments.callee(path + '/' + children[i]);
    }
    ak.fs._remove(path);
  };


  ak.Data.prototype.setProp('toString',
                            ak.DONT_ENUM,
                            ak.Data.prototype._toString);
})();
