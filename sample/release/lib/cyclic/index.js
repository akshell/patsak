var a = require('a');
var b = require('b');
exports.pass = a.a && b.b && a.a().b === b.b && b.b().a === a.a;
