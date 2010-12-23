// (c) 2010 by Anton Korenyushkin


var doQuery = exports.query;

exports.query = function (query,
                          queryParams/* = [] */,
                          by/* = [] */,
                          byParams/* = [] */,
                          start/* = 0 */,
                          length/* optional */) {
  if (!arguments.length)
    throw TypeError('At least 1 argument required');
  return doQuery(query,
                 queryParams || [],
                 by ? (by instanceof Array ? by : [by]) : [],
                 byParams || [],
                 start || 0,
                 length);
};


exports.dropAll = function () {
  exports.drop(exports.list());
};
