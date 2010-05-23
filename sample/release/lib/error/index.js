exports.pass = true;
for (var i = 0; i < 2; ++i) {
  try {
    require('error');
    exports.pass = false;
  } catch (error) {}
}
