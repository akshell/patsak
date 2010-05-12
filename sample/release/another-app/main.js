
_core.main = function (arg) {
  var file_contents = [];
  for (var i = 0; i < _core.files.length; ++i)
    file_contents.push(_core.files[i]._toString());
  return JSON.stringify(
    {
      user: _core.user,
      arg: arg,
      data: _core.data._toString(),
      fileContents: file_contents,
      issuer: _core.issuer
    });
};
