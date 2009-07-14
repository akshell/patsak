
import('ak', 'json2.js');

ak._main = function (arg)
{
    var file_contents = [];
    for (var i = 0; i < ak._files.length; ++i) {
        file_contents.push(ak.fs.read(ak._files[i]).toString());
        ak.fs.rm(ak._files[i]);
    }
    return JSON.stringify({user: ak._user,
                           arg: arg,
                           data: ak._data.toString(),
                           file_contents: file_contents,
                           requester_app: ak._requester_app});
};
