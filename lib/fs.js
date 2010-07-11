// (c) 2010 by Anton Korenyushkin


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
