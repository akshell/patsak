
_core.main = function (arg) {
  var file_contents = [];
  for (var i = 0; i < _core.files.length; ++i) {
    file_contents.push(_core.fs.read(_core.files[i])._toString());
    _core.fs.remove(_core.files[i]);
  }
  return JSON.stringify(
    {
      user: _core.user,
      arg: arg,
      data: _core.data._toString(),
      fileContents: file_contents,
      issuer: _core.issuer
    });
};
