// (c) 2010 by Anton Korenyushkin


exports.FileStorage.prototype.removeAll = function () {
  this.list('').forEach(this.remove, this);
};
