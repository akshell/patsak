// (c) 2010-2011 by Anton Korenyushkin

#include "js-http-parser.h"
#include "js-common.h"
#include "js-binary.h"

#include <http_parser.h>


using namespace ak;
using namespace v8;
using namespace std;


////////////////////////////////////////////////////////////////////////////////
// HttpParserBg
////////////////////////////////////////////////////////////////////////////////

#define DEFINE_HTTP_CALLBACK(name)                                  \
    static int On##name(http_parser *p) {                           \
        HttpParserBg* parser = static_cast<HttpParserBg*>(p->data); \
        Handle<v8::Value> value(Get(parser->handler_, "on" #name)); \
        if (!value->IsFunction())                                   \
            return 0;                                               \
        Handle<Function> func(Handle<Function>::Cast(value));       \
        Handle<v8::Value> ret(func->Call(parser->handler_, 0, 0));  \
        if (ret.IsEmpty()) {                                        \
            parser->got_exception_ = true;                          \
            return -1;                                              \
        } else {                                                    \
            return 0;                                               \
        }                                                           \
    }


#define DEFINE_HTTP_DATA_CALLBACK(name)                                 \
    static int On##name(http_parser *p, const char *at, size_t size) {  \
        HttpParserBg* parser = static_cast<HttpParserBg*>(p->data);     \
        AK_ASSERT(parser->binary_ptr_);                                 \
        Handle<v8::Value> value(Get(parser->handler_, "on" #name));     \
        if (!value->IsFunction())                                       \
            return 0;                                                   \
        Handle<Function> func(Handle<Function>::Cast(value));           \
        Binarizator binarizator(*parser->binary_ptr_);                  \
        size_t start = at - binarizator.GetData();                      \
        Handle<v8::Value> binary(                                       \
            NewBinary(*parser->binary_ptr_, start, start + size));      \
        Handle<v8::Value> ret(                                          \
            func->Call(parser->handler_, 1, &binary));                  \
        if (ret.IsEmpty()) {                                            \
            parser->got_exception_ = true;                              \
            return -1;                                                  \
        } else {                                                        \
            return 0;                                                   \
        }                                                               \
    }


namespace
{
    class HttpParserBg {
    public:
        DECLARE_JS_CONSTRUCTOR(HttpParserBg);

        HttpParserBg(http_parser_type type, Handle<Object> handler);
        ~HttpParserBg();

    private:
        DEFINE_HTTP_CALLBACK(MessageBegin)
        DEFINE_HTTP_CALLBACK(MessageComplete)

        DEFINE_HTTP_DATA_CALLBACK(Path)
        DEFINE_HTTP_DATA_CALLBACK(URI)
        DEFINE_HTTP_DATA_CALLBACK(Fragment)
        DEFINE_HTTP_DATA_CALLBACK(QueryString)
        DEFINE_HTTP_DATA_CALLBACK(HeaderField)
        DEFINE_HTTP_DATA_CALLBACK(HeaderValue)
        DEFINE_HTTP_DATA_CALLBACK(Body)

        static string GetMethodName(unsigned char method);
        static int OnHeadersComplete(http_parser* p);
        static http_parser_settings CreateSettings();

        Persistent<Object> handler_;
        bool got_exception_;
        http_parser impl_;
        BinaryBg* binary_ptr_;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, ExecCb, const Arguments&);
    };
}


DEFINE_JS_CONSTRUCTOR(HttpParserBg, "HttpParser",
                      /*object_template*/, proto_template)
{
    SetFunction(proto_template, "exec", ExecCb);
}


DEFINE_JS_CONSTRUCTOR_CALLBACK(HttpParserBg, args)
{
    CheckArgsLength(args, 2);
    string type_name(Stringify(args[0]));
    http_parser_type type;
    if (type_name == "request")
        type = HTTP_REQUEST;
    else if (type_name == "response")
        type = HTTP_RESPONSE;
    else
        throw Error(Error::VALUE,
                    "HTTPParser type must be 'request' or 'response'");
    if (!args[1]->IsObject())
        throw Error(Error::TYPE, "Object required");
    return new HttpParserBg(type, args[1]->ToObject());
}


HttpParserBg::HttpParserBg(http_parser_type type, Handle<Object> handler)
    : handler_(Persistent<Object>::New(handler))
{
    http_parser_init(&impl_, type);
    impl_.data = this;
}


HttpParserBg::~HttpParserBg()
{
    handler_.Dispose();
}


string HttpParserBg::GetMethodName(unsigned char method)
{
    switch (method) {
    case HTTP_DELETE:    return "DELETE";
    case HTTP_GET:       return "GET";
    case HTTP_HEAD:      return "HEAD";
    case HTTP_POST:      return "POST";
    case HTTP_PUT:       return "PUT";
    case HTTP_CONNECT:   return "CONNECT";
    case HTTP_OPTIONS:   return "OPTIONS";
    case HTTP_TRACE:     return "TRACE";
    case HTTP_COPY:      return "COPY";
    case HTTP_LOCK:      return "LOCK";
    case HTTP_MKCOL:     return "MKCOL";
    case HTTP_MOVE:      return "MOVE";
    case HTTP_PROPFIND:  return "PROPFIND";
    case HTTP_PROPPATCH: return "PROPPATCH";
    case HTTP_UNLOCK:    return "UNLOCK";
    default:             return "UNKNOWN_METHOD";
    }
}


int HttpParserBg::OnHeadersComplete(http_parser* p)
{
    HttpParserBg* parser = static_cast<HttpParserBg*>(p->data);
    Handle<v8::Value> value(Get(parser->handler_, "onHeadersComplete"));
    if (!value->IsFunction())
        return 0;
    Handle<Function> func(Handle<Function>::Cast(value));
    Handle<Object> object(Object::New());
    if (p->type == HTTP_REQUEST)
        Set(object,
            "method",
            String::NewSymbol(GetMethodName(p->method).c_str()));
    else
        Set(object, "status", Integer::New(p->status_code));
    Set(object, "versionMajor", Integer::New(p->http_major));
    Set(object, "versionMinor", Integer::New(p->http_minor));
    Set(object, "shouldKeepAlive", Boolean::New(http_should_keep_alive(p)));
    Set(object, "upgrade", Boolean::New(p->upgrade));
    Handle<v8::Value> arg(object);
    Handle<v8::Value> is_head_response(func->Call(parser->handler_, 1, &arg));
    if (is_head_response.IsEmpty()) {
        parser->got_exception_ = true;
        return -1;
    } else {
        return is_head_response->IsTrue() ? 1 : 0;
    }
}


http_parser_settings HttpParserBg::CreateSettings()
{
    http_parser_settings settings;
    settings.on_message_begin    = OnMessageBegin;
    settings.on_message_complete = OnMessageComplete;
    settings.on_path             = OnPath;
    settings.on_url              = OnURI;
    settings.on_fragment         = OnFragment;
    settings.on_query_string     = OnQueryString;
    settings.on_header_field     = OnHeaderField;
    settings.on_header_value     = OnHeaderValue;
    settings.on_body             = OnBody;
    settings.on_headers_complete = OnHeadersComplete;
    return settings;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, HttpParserBg, ExecCb,
                    const Arguments&, args)
{
    static http_parser_settings settings(CreateSettings());
    CheckArgsLength(args, 1);
    binary_ptr_ = CastToBinary(args[0]);
    if (!binary_ptr_)
        throw Error(Error::TYPE, "Binary required");
    got_exception_ = false;
    Binarizator binarizator(*binary_ptr_);
    size_t parsed = http_parser_execute(
        &impl_, &settings, binarizator.GetData(), binarizator.GetSize());
    binary_ptr_ = 0;
    if (got_exception_)
        return Handle<v8::Value>();
    if (parsed != binarizator.GetSize())
        throw Error(Error::VALUE, "Parsing failed");
    return Undefined();
}

////////////////////////////////////////////////////////////////////////////////
// InitHTTP
////////////////////////////////////////////////////////////////////////////////

Handle<Object> ak::InitHttpParser()
{
    Handle<Object> result(Object::New());
    PutClass<HttpParserBg>(result);
    return result;
}
