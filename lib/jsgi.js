// (c) 2011 by Anton Korenyushkin

var core = require('core');
var HttpParser = require('http-parser').HttpParser;
var Binary = require('binary').Binary;


var colon = new Binary(': ');
var crlf = new Binary('\r\n');


require.main.exports.handle = function (socket) {
  var request = {
    scheme: 'http',
    port: 80,
    scriptName: '',
    pathInfo: '',
    queryString: '',
    headers: {},
    jsgi: {
      version: [0, 3],
      multithread: false,
      multiprocess: true,
      runOnce: false,
      cgi: false
    },
    env: {}
  };

  var name = '';
  var value;
  var inputParts = [];
  var complete = false;

  function addHeader() {
    name = name.toLowerCase();
    if (request.headers.hasOwnProperty(name))
      request.headers[name] += ',' + value;
    else
      request.headers[name] = value;
  }

  var parser = new HttpParser(
    'request',
    {
      onPath: function (part) {
        request.pathInfo += part;
      },

      onQueryString: function (part) {
        request.queryString += part;
      },

      onHeaderField: function (part) {
        if (value === undefined) {
          name += part;
        } else {
          addHeader();
          name = part + '';
          value = undefined;
        }
      },

      onHeaderValue: function (part) {
        value = (value || '') + part;
      },

      onHeadersComplete: function (info) {
        if (value !== undefined)
          addHeader();
        request.method = info.method.toUpperCase();
        request.host = request.headers.host || '';
        request.version = [info.versionMajor, info.versionMinor];
      },

      onBody: function (part) {
        inputParts.push(part);
      },

      onMessageComplete: function () {
        complete = true;
      }
    });

  while (!complete)
    parser.exec(socket.receive(8192));
  request.input = core.construct(Binary, inputParts);

  var response = require.main.exports.app(request);

  var parts = [new Binary('HTTP/1.1 ' + response.status), crlf];
  for (name in response.headers) {
    var values = response.headers[name];
    if (!(values instanceof Array))
      values = [values];
    values.forEach(
      function (value) {
        parts.push(new Binary(name), colon, new Binary(value), crlf);
      });
  }
  parts.push(crlf);
  socket.write(core.construct(Binary, parts));
  response.body.forEach(function (part) { socket.write(new Binary(part)); });
};
