// (c) 2010 by Anton Korenyushkin


main = function (socket) {
  socket.send(
    'HTTP/1.1 200 OK\r\n' +
    'Content-Type: text/plain\r\n' +
    'Content-Length: 12\r\n' +
    '\r\n' +
    'Hello world!');
};
