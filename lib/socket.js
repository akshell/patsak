// (c) 2010-2011 by Anton Korenyushkin

var core = require('core');
var Binary = require('binary').Binary;


exports.Socket.prototype.read = function (size/* optional */) {
  if (!arguments.length)
    size = Infinity;
  var chunks = [];
  var received = 0;
  do {
    var chunk = this.receive(size == Infinity ? 8192 : size - received);
    if (!chunk.length)
      break;
    chunks.push(chunk);
    received += chunk.length;
  } while (received < size);
  return chunks.length == 1 ? chunks[0] : core.construct(Binary, chunks);
};


exports.Socket.prototype.write = function (data) {
  if (!(data instanceof Binary))
    data = new Binary(data + '');
  var sent = 0;
  do {
    var count = this.send(data.range(sent));
    sent += count;
  } while (count && sent < data.length);
  return sent;
};
