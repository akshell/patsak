var d = require('b/c/../c//./d').module;
exports.pass = (
  module.app == 'lib' &&
  module.version == 'module/a' &&
  module.id == 'index' &&
  d.app == 'lib' &&
  d.version == 'module/a' &&
  d.id == 'b/c/d');
