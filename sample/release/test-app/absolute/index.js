var a = require('absolute/submodule/a');
var b = require('absolute/b');
exports.pass = a.foo().foo === b.foo;
