// (c) 2010-2011 by Anton Korenyushkin


var doList = exports.FileStorage.prototype.list;

exports.FileStorage.prototype.list = function (path) {
  return doList.call(this, path).sort();
};
