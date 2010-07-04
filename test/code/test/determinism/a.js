exports.pass = false;
try {
  require('a');
} catch (exception) {
  exports.pass = true;
}
