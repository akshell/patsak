// (c) 2010-2011 by Anton Korenyushkin

var core = require('core');
var fs = require('fs');
var Binary = require('binary').Binary;


if (!exports.Repo)
  throw core.RequireError('git is disabled');


var prefixes = ['', 'refs/', 'refs/tags/', 'refs/heads/'];


exports.Repo.prototype.deref = function (ref) {
  for (var i = 0; i < prefixes.length; ++i) {
    var prefix = prefixes[i];
    if (this.refs.hasOwnProperty(prefix + ref)) {
      var link = this.refs[prefix + ref];
      while (link.substr(0, 5) == 'ref: ')
        link = this.refs[link.substr(5)];
      return link;
    }
  }
  throw new core.ValueError('No such reference: ' + ref);
};


exports.Repo.prototype.getStorage = function (ref) {
  return new exports.GitStorage(this, ref);
};


exports.GitStorage = function (repo, ref) {
  this.repo = repo;
  this.ref = ref;
  try {
    var object = repo.readObject(ref);
    var sha = ref;
  } catch (_) {
    sha = repo.deref(ref);
    object = repo.readObject(sha);
  }
  if (object.type == 'tag') {
    sha = object.data.range(7, 47);
    object = repo.readObject(sha);
  }
  if (object.type != 'commit')
    throw core.ValueError('Non-commit git object');
  this.commit = sha + '';
  this._root = this._walk(object.data.range(5, 45));
};


exports.GitStorage.prototype._walk = function (sha) {
  var result = {};
  var data = this.repo.readObject(sha).data;
  do {
    var spaceIdx = data.indexOf(' ');
    var nullIdx = data.indexOf('\0', spaceIdx);
    var childSHA = data.range(nullIdx + 1, nullIdx + 21);
    result[data.range(spaceIdx + 1, nullIdx)] =
      data[0] == 52 ? this._walk(childSHA) : new Binary(childSHA);
    data = data.range(nullIdx + 21);
  } while (data.length);
  return result;
};


exports.GitStorage.prototype._get = function (path) {
  var stack = [];
  var last = this._root;
  var names = path.split('/');
  for (var i = 0; i < names.length; ++i) {
    var name = names[i];
    if (last instanceof Binary)
      return null;
    switch (name) {
    case '':
    case '.':
      break;
    case '..':
      if (!stack.length)
        throw core.ValueError('Invalid repository path: ' + path);
      last = stack.pop();
      break;
    default:
      if (!last.hasOwnProperty(name))
        return null;
      stack.push(last);
      last = last[name];
    }
  }
  return last;
};


exports.GitStorage.prototype.list = function (path) {
  var entry = this._get(path);
  if (!entry)
    throw fs.NoSuchEntryError('No such entry');
  if (entry instanceof Binary)
    throw fs.EntryIsFileError('Entry is file');
  var names = [];
  for (var name in entry)
    names.push(name);
  return names.sort();
};


exports.GitStorage.prototype.exists = function (path) {
  return !!this._get(path);
};


exports.GitStorage.prototype.isFile = function (path) {
  return this._get(path) instanceof Binary;
};


exports.GitStorage.prototype.isFolder = function (path) {
  var entry = this._get(path);
  return entry && !(entry instanceof Binary);
};


exports.GitStorage.prototype.read = function (path) {
  var entry = this._get(path);
  if (!entry)
    throw fs.NoSuchEntryError('No such entry');
  if (!(entry instanceof Binary))
    throw fs.EntryIsFolderError('Entry is folder');
  return this.repo.readObject(entry).data;
};
