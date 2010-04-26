var a = require('submodule/a');
var b = require('b');
exports.pass = a.foo().foo === b.foo;
