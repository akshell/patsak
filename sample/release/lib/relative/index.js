var a = require('submodule/a');
var b = require('submodule/b');
exports.pass = a.foo == b.foo;
