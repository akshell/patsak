// (c) 2010 by Anton Korenyushkin

#ifndef JS_SOCKET_H
#define JS_SOCKET_H

#include <v8.h>


namespace ak
{
    class SocketScope {
    public:
        SocketScope(int fd);
        ~SocketScope();
        v8::Handle<v8::Object> GetSocket() const;

    private:
        v8::Handle<v8::Object> socket_;
    };


    v8::Handle<v8::Object> InitSocket();
}

#endif // JS_SOCKET_H
