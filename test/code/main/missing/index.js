exports.pass = false;
try {
  require('bogus');
} catch (exception) {
  exports.pass = true;
}
