
// (c) 2010 by Anton Korenyushkin

#include "js-socket.h"
#include "js-common.h"
#include "js-binary.h"

#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>


using namespace ak;
using namespace v8;
using namespace std;


////////////////////////////////////////////////////////////////////////////////
// SocketBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    const size_t MAX_OPEN_COUNT = 100;
    size_t open_count = 0;


    class SocketBg {
    public:
        DECLARE_JS_CLASS(SocketBg);

        SocketBg(int fd);
        SocketBg(const std::string& host, const std::string& service);
        ~SocketBg();

        void Close();

    private:
        int fd_;
        bool readable_;
        bool writable_;

        void CheckOpen() const;

        DECLARE_JS_CALLBACK2(v8::Handle<v8::Value>, GetClosedCb,
                             v8::Local<v8::String>,
                             const v8::AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(v8::Handle<v8::Value>, GetReadableCb,
                             v8::Local<v8::String>,
                             const v8::AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(v8::Handle<v8::Value>, GetWritableCb,
                             v8::Local<v8::String>,
                             const v8::AccessorInfo&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, CloseCb,
                             const v8::Arguments&);

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ReceiveCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, SendCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ShutdownCb,
                             const v8::Arguments&);
    };
}


DEFINE_JS_CLASS(SocketBg, "Socket", object_template, proto_template)
{
    object_template->SetAccessor(String::NewSymbol("closed"),
                                 GetClosedCb, 0,
                                 Handle<v8::Value>(), DEFAULT,
                                 ReadOnly | DontDelete);
    object_template->SetAccessor(String::NewSymbol("readable"),
                                 GetReadableCb, 0,
                                 Handle<v8::Value>(), DEFAULT,
                                 ReadOnly | DontDelete);
    object_template->SetAccessor(String::NewSymbol("writable"),
                                 GetWritableCb, 0,
                                 Handle<v8::Value>(), DEFAULT,
                                 ReadOnly | DontDelete);
    SetFunction(proto_template, "close", CloseCb);
    SetFunction(proto_template, "receive", ReceiveCb);
    SetFunction(proto_template, "send", SendCb);
    SetFunction(proto_template, "shutdown", ShutdownCb);
}


SocketBg::SocketBg(int fd)
    : fd_(fd)
    , readable_(true)
    , writable_(true)
{
    ++open_count;
}


SocketBg::SocketBg(const string& host, const string& service)
    : readable_(true)
    , writable_(true)
{
    if (open_count >= MAX_OPEN_COUNT)
        throw Error(Error::QUOTA, "Too many open sockets");
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* first_info_ptr;
    if (int ret = getaddrinfo(host.c_str(), service.c_str(),
                              &hints, &first_info_ptr))
        throw Error(Error::SOCKET, gai_strerror(ret));
    struct addrinfo* info_ptr = first_info_ptr;
    for (; info_ptr; info_ptr = info_ptr->ai_next) {
        fd_ = socket(info_ptr->ai_family,
                     info_ptr->ai_socktype,
                     info_ptr->ai_protocol);
        AK_ASSERT(fd_ != -1);
        if (connect(fd_, info_ptr->ai_addr, info_ptr->ai_addrlen) != -1)
            break;
        close(fd_);
    }
    freeaddrinfo(first_info_ptr);
    if (!info_ptr)
        throw Error(Error::SOCKET, "Failed to connect");
    ++open_count;
}


SocketBg::~SocketBg()
{
    Close();
}


void SocketBg::Close()
{
    if (fd_ != -1) {
        close(fd_);
        fd_ = -1;
        --open_count;
    }
}


void SocketBg::CheckOpen() const
{
    if (fd_ == -1)
        throw Error(Error::VALUE, "Socket is already closed");
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, SocketBg, GetClosedCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/) const
{
    return Boolean::New(fd_ == -1);
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, SocketBg, GetReadableCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/) const
{
    CheckOpen();
    return Boolean::New(readable_);
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, SocketBg, GetWritableCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/) const
{
    CheckOpen();
    return Boolean::New(writable_);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, SocketBg, CloseCb,
                    const Arguments&, /*args*/)
{
    Close();
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, SocketBg, ReceiveCb,
                    const Arguments&, args) const
{
    CheckOpen();
    if (!readable_)
        throw Error(Error::VALUE, "Socket is shut down for receiving");
    CheckArgsLength(args, 1);
    size_t size = args[0]->Uint32Value();
    auto_ptr<Chars> data_ptr(new Chars(size));
    ssize_t received = recv(fd_, &data_ptr->front(), size, 0);
    if (received == -1)
        throw Error(Error::SOCKET, strerror(errno));
    data_ptr->resize(received);
    return NewBinary(data_ptr);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, SocketBg, SendCb,
                    const Arguments&, args) const
{
    CheckOpen();
    if (!writable_)
        throw Error(Error::VALUE, "Socket is shut down for sending");
    CheckArgsLength(args, 1);
    Binarizator binarizator(args[0]);
    ssize_t sent = send(fd_, binarizator.GetData(), binarizator.GetSize(), 0);
    if (sent == -1)
        throw Error(Error::SOCKET, strerror(errno));
    return Integer::New(sent);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, SocketBg, ShutdownCb,
                    const Arguments&, args)
{
    CheckOpen();
    CheckArgsLength(args, 1);
    int type;
    string type_name(Stringify(args[0]));
    if (type_name == "send") {
        type = SHUT_WR;
        writable_ = false;
    } else if (type_name == "receive") {
        type = SHUT_RD;
        readable_ = false;
    } else if (type_name == "both") {
        type = SHUT_RDWR;
        readable_ = writable_ = false;
    } else {
        throw Error(Error::VALUE,
                    "Valid shutdown types are 'send', 'receive', and 'both'");
    }
    if (shutdown(fd_, type))
        throw Error(Error::SOCKET, strerror(errno));
    return Undefined();
}

////////////////////////////////////////////////////////////////////////////////
// SocketScope definitions
////////////////////////////////////////////////////////////////////////////////

SocketScope::SocketScope(int fd)
    : socket_(JSNew<SocketBg>(fd))
{
}


SocketScope::~SocketScope()
{
    SocketBg::GetJSClass().Cast(socket_)->Close();
}


Handle<Object> SocketScope::GetSocket() const
{
    return socket_;
}

////////////////////////////////////////////////////////////////////////////////
// InitSocket
////////////////////////////////////////////////////////////////////////////////

namespace
{
    DEFINE_JS_FUNCTION(ConnectCb, args)
    {
        CheckArgsLength(args, 2);
        return JSNew<SocketBg>(Stringify(args[0]), Stringify(args[1]));
    }
}


Handle<Object> ak::InitSocket()
{
    Handle<Object> result(Object::New());
    SetFunction(result, "connect", ConnectCb);
    PutClass<SocketBg>(result);
    return result;
}
