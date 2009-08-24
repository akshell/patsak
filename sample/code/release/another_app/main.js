
eval(ak._readCode('ak', 'json2.js'));

ak._main = function (arg)
{
    var file_contents = [];
    for (var i = 0; i < ak._files.length; ++i) {
        file_contents.push(ak.fs._read(ak._files[i])._toString());
        ak.fs._remove(ak._files[i]);
    }
    return JSON.stringify({user: ak._user,
                           arg: arg,
                           data: ak._data._toString(),
                           file_contents: file_contents,
                           requester_app: ak._requesterAppName});
};
