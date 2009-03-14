
function json(smth) {
    switch (typeof(smth)) {
    case 'string':
        return '"' + smth.replace(/\"/g, '\\"') + '"';
    case 'number':
        return smth;
    case 'object':
        if (smth instanceof Array)
            return jsonArray(smth);
        else if (smth instanceof Function)
            return smth.toString();
        return jsonObject(smth);
    };
    return smth.toString();
}

function jsonArray(arr) {
    var results = [];
    for (var i = 0; i < arr.length; ++i)
        results.push(json(arr[i]));
    return '[' + results.join(', ') + ']';
}

function jsonObject(obj) {
    var results = [];
    for (field in obj)
        results.push(json(field) + ': ' + json(obj[field]));
    return '{' + results.join(', ') + '}';

};

////////////////////////////////////////////////////////////////////////////////

function map(seq, func) {
    var result = new Array(seq.length);
    for (var i = 0; i < seq.length; ++i)
        result[i] = func(seq[i]);
    return result;
}

var author_re = /author\/(\d+)/;
var post_re = /post\/(\d+)/;

function dispatch(url) {
    var match = url.match(author_re);
    if (match) {
        var posts_raw = query('Post where Post.author == ' + match[1]);
        var posts = map(posts_raw,
                        function (raw) {
                            return {title: raw.title,
                                    text: raw.text,
                                    post_href: '/post/' + raw.id};
                        });
        var author = query('User where User.id == ' + match[1])[0].name;
        return json({posts: posts, author: author});
    }
    match = url.match(post_re);
    if (match) {
        var post_raw = query('Post where Post.id == ' + match[1])[0];
        var comments_raw = query('Comment where Comment.post == ' + match[1]);
        var post = {title: post_raw.title,
                    text: post_raw.text,
                    author_href: '/author/' + post_raw.author,
                    author:  query('User where User.id == ' + post_raw.author)[0].name};
        comments_raw.sort(function (c1, c2) { return c1.id - c2.id; });
        var comments = map(comments_raw,
                           function (raw) {
                               return {text: raw.text,
                                       author_href: '/author/' + raw.author,
                                       author: query('User where User.id == ' + raw.author)[0].name};
                           });
        return json({post: post,
                     comments: comments});
    }
    return undefined;
}

// print(json([dispatch('author/1'), dispatch('post/0')]));
// print('\n');
