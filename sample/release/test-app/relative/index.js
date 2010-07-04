var a = require('./submodule/a');
var b = require('relative/submodule/b');
exports.pass = a.foo == b.foo;
