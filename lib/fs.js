// (c) 2010 by Anton Korenyushkin


exports.FileStorage.prototype.list = function (path) {
  return this._list(path).sort();
};
