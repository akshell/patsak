// (c) 2010 by Anton Korenyushkin

var core = require('core');


exports.FileStorage.prototype.removeAll = function () {
  this.list('').forEach(this.remove, this);
};


exports.FileStorage.prototype.read = function (path) {
  var file = this.open(path);
  try {
    return file.read();
  } finally {
    file.close();
  }
};


exports.FileStorage.prototype.write = function (path, data) {
  var file = this.open(path);
  try {
    file.length = 0;
    file.write(data);
  } finally {
    file.close();
  }
};


['readable', 'positionable'].forEach(
  function (name) {
    exports.File.prototype.__defineGetter__(
      name,
      function () {
        if (this.closed)
          throw core.ValueError('File is already closed');
        return true;
      });
  });
