// (c) 2010-2011 by Anton Korenyushkin


var table = '0123456789abcdef';


['md5', 'sha1'].forEach(
  function (name) {
    var func = exports.Binary.prototype[name];
    exports.Binary.prototype[name] = function (raw/* = false */) {
      var hash = func.call(this);
      if (raw)
        return hash;
      var bytes = [];
      for (var i = 0; i < hash.length; ++i) {
        var code = hash[i];
        bytes.push(table[code >> 4], table[code & 0xF]);
      }
      return bytes.join('');
    };
  });
