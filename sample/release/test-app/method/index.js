var a = require('./a');
var foo = a.foo;
a.set(10);
exports.pass = a.foo() == a && foo() == this && a.get() == 10;
