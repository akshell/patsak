
// (c) 2009-2010 by Anton Korenyushkin

#include "js.h"
#include "js-db.h"
#include "js-file.h"
#include "db.h"

#include <boost/foreach.hpp>
#include <http_parser.h>

#include <setjmp.h>


using namespace ak;
using namespace std;
using namespace v8;


////////////////////////////////////////////////////////////////////////////////
// Constants
////////////////////////////////////////////////////////////////////////////////

// File generated from init.js script by xxd -i, INIT_JS is defined here
#include "init.js.h"


namespace
{
    const int MAX_YOUNG_SPACE_SIZE =  2 * 1024 * 1024;
    const int MAX_OLD_SPACE_SIZE   = 32 * 1024 * 1024;
    const int STACK_LIMIT          =  2 * 1024 * 1024;
}

////////////////////////////////////////////////////////////////////////////////
// CodeReader
////////////////////////////////////////////////////////////////////////////////

namespace
{
    // Class for read access to code files of current and other applications
    class CodeReader {
    public:
        CodeReader(const string& app_path, const string& release_path);
        auto_ptr<Chars> Read(const string& path) const;
        auto_ptr<Chars> Read(const string& app_name, const string& path) const;
        time_t GetModDate(const string& path) const;
        time_t GetModDate(const string& app_name, const string& path) const;

    private:
        string app_path_;
        string release_path_;

        static void CheckAppName(const string& app_name);
        static void CheckPath(const string& path);

        auto_ptr<Chars> DoRead(const string& base_path,
                               const string& path) const;

        time_t DoGetModDate(const string& base_path, const string& path) const;
    };
}


CodeReader::CodeReader(const string& app_path, const string& release_path)
    : app_path_(app_path), release_path_(release_path)
{
}


auto_ptr<Chars> CodeReader::Read(const string& path) const
{
    return DoRead(app_path_, path);
}


auto_ptr<Chars> CodeReader::Read(const string& app_name,
                                 const string& path) const
{
    CheckAppName(app_name);
    return DoRead(release_path_ + '/' + app_name, path);
}


time_t CodeReader::GetModDate(const string& path) const
{
    return DoGetModDate(app_path_, path);
}


time_t CodeReader::GetModDate(const string& app_name, const string& path) const
{
    CheckAppName(app_name);
    return DoGetModDate(release_path_ + '/' + app_name, path);
}


void CodeReader::CheckAppName(const string& app_name)
{
    BOOST_FOREACH(char c, app_name)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            throw Error(Error::VALUE, "Illegal app name");
}


void CodeReader::CheckPath(const string& path)
{
    if (GetPathDepth(path) <= 0)
        throw Error(Error::PATH, "Code path \"" + path + "\" is illegal");
}


auto_ptr<Chars> CodeReader::DoRead(const string& base_path,
                                   const string& path) const
{
    CheckPath(path);
    return ReadFile(base_path + '/' + path);
}


time_t CodeReader::DoGetModDate(const string& base_path,
                                const string& path) const
{
    CheckPath(path);
    return GetStat(base_path + '/' + path)->st_mtime;
}

////////////////////////////////////////////////////////////////////////////////
// ScriptBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class ScriptBg {
    public:
        DECLARE_JS_CLASS(ScriptBg);

        ScriptBg(Handle<Script> script);
        ~ScriptBg();

    private:
        Persistent<Script> script_;

        static Handle<v8::Value> ConstructorCb(const Arguments& args);

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, RunCb,
                             const Arguments&) const;
    };
}


DEFINE_JS_CONSTRUCTOR(ScriptBg, "Script", ConstructorCb,
                      /*object_template*/, proto_template)
{
    SetFunction(proto_template, "_run", RunCb);
}


Handle<v8::Value> ScriptBg::ConstructorCb(const Arguments& args)
{
    if (!args.IsConstructCall())
        return Undefined();
    try {
        CheckArgsLength(args, 1);
        auto_ptr<ScriptOrigin> origin_ptr;
        if (args.Length() > 1) {
            Handle<Integer> line_offset, column_offset;
            if (args.Length() > 2)
                line_offset = args[2]->ToInteger();
            if (args.Length() > 3)
                column_offset = args[3]->ToInteger();
            origin_ptr.reset(
                new ScriptOrigin(args[1], line_offset, column_offset));
        }
        Handle<Script> script(
            Script::Compile(args[0]->ToString(), origin_ptr.get()));
        if (!script.IsEmpty())
            ScriptBg::GetJSClass().Attach(args.This(), new ScriptBg(script));
        return Handle<v8::Value>();
    } JS_CATCH(Handle<v8::Value>);
}


ScriptBg::ScriptBg(Handle<Script> script)
    : script_(Persistent<Script>::New(script))
{
}


ScriptBg::~ScriptBg()
{
    script_.Dispose();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, ScriptBg, RunCb,
                    const Arguments&, /*args*/) const
{
    return script_->Run();
}

////////////////////////////////////////////////////////////////////////////////
// ProxyBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class ProxyBg {
    public:
        DECLARE_JS_CLASS(ProxyBg);

        ProxyBg(Handle<Object> handler);
        ~ProxyBg();

    private:
        Persistent<Object> handler_;

        static Handle<v8::Value> ConstructorCb(const Arguments& args);

        Handle<v8::Value> Call(const string& name,
                               int argc,
                               Handle<v8::Value> argv[]) const;

        Handle<Boolean> CallIndicator(const string& name,
                                      Handle<v8::Value> arg) const;

        DECLARE_JS_CALLBACK2(Handle<v8::Value>, GetNamedCb,
                             Local<String>, const AccessorInfo&) const;

        DECLARE_JS_CALLBACK3(Handle<v8::Value>, SetNamedCb,
                             Local<String>,
                             Local<v8::Value>,
                             const AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(Handle<Boolean>, QueryNamedCb,
                             Local<String>, const AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(Handle<Boolean>, DeleteNamedCb,
                             Local<String>, const AccessorInfo&) const;

        DECLARE_JS_CALLBACK1(Handle<Array>, EnumCb,
                             const AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(Handle<v8::Value>, GetIndexedCb,
                             uint32_t, const AccessorInfo&) const;

        DECLARE_JS_CALLBACK3(Handle<v8::Value>, SetIndexedCb,
                             uint32_t,
                             Local<v8::Value>,
                             const AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(Handle<Boolean>, QueryIndexedCb,
                             uint32_t, const AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(Handle<Boolean>, DeleteIndexedCb,
                             uint32_t, const AccessorInfo&) const;
    };
}


DEFINE_JS_CONSTRUCTOR(ProxyBg, "Proxy", ConstructorCb,
                      object_template, /*proto_template*/)
{
    object_template->SetNamedPropertyHandler(GetNamedCb,
                                             SetNamedCb,
                                             QueryNamedCb,
                                             DeleteNamedCb,
                                             EnumCb);
    object_template->SetIndexedPropertyHandler(GetIndexedCb,
                                               SetIndexedCb,
                                               QueryIndexedCb,
                                               DeleteIndexedCb);
}


Handle<v8::Value> ProxyBg::ConstructorCb(const Arguments& args)
{
    if (!args.IsConstructCall())
        return Undefined();
    try {
        CheckArgsLength(args, 1);
        if (!args[0]->IsObject())
            throw Error(Error::TYPE, "Object required");
        ProxyBg::GetJSClass().Attach(args.This(),
                                     new ProxyBg(args[0]->ToObject()));
        return Handle<v8::Value>();
    } JS_CATCH(Handle<v8::Value>);
}


ProxyBg::ProxyBg(Handle<Object> handler)
    : handler_(Persistent<Object>::New(handler))
{
}


ProxyBg::~ProxyBg()
{
    handler_.Dispose();
}


Handle<v8::Value> ProxyBg::Call(const string& name,
                                int argc,
                                Handle<v8::Value> argv[]) const
{
    Handle<v8::Value> func(Get(handler_, name));
    if (func.IsEmpty())
        return Handle<v8::Value>();
    if (!func->IsFunction())
        throw Error(Error::TYPE, name + " is not a function");
    return Handle<Function>::Cast(func)->Call(handler_, argc, argv);
}


Handle<Boolean> ProxyBg::CallIndicator(const string& name,
                                  Handle<v8::Value> arg) const
{
    TryCatch try_catch;
    Handle<v8::Value> value(Call(name, 1, &arg));
    return value.IsEmpty() ? Boolean::New(false) : value->ToBoolean();
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, ProxyBg, GetNamedCb,
                    Local<String>, property,
                    const AccessorInfo&, /*info*/) const
{
    Handle<v8::Value> arg(property);
    return Call("get", 1, &arg);
}


DEFINE_JS_CALLBACK3(Handle<v8::Value>, ProxyBg, SetNamedCb,
                    Local<String>, property,
                    Local<v8::Value>, value,
                    const AccessorInfo&, /*info*/) const
{
    Handle<v8::Value> argv[] = {property, value};
    return Call("set", 2, argv);
}


DEFINE_JS_CALLBACK2(Handle<Boolean>, ProxyBg, QueryNamedCb,
                    Local<String>, property,
                    const AccessorInfo&, /*info*/) const
{
    return CallIndicator("query", property);
}


DEFINE_JS_CALLBACK2(Handle<Boolean>, ProxyBg, DeleteNamedCb,
                    Local<String>, property,
                    const AccessorInfo&, /*info*/) const
{
    return CallIndicator("del", property);
}


DEFINE_JS_CALLBACK1(Handle<Array>, ProxyBg, EnumCb,
                    const AccessorInfo&, /*info*/) const
{
    TryCatch try_catch;
    Handle<v8::Value> ret(Call("list", 0, 0));
    if (ret.IsEmpty())
        return Handle<Array>();
    if (!ret->IsObject())
        return Array::New();
    Handle<Object> object(ret->ToObject());
    Handle<v8::Value> length_value(Get(object, "length"));
    if (length_value.IsEmpty())
        return Handle<Array>();
    if (!length_value->IsInt32())
        return Array::New();
    int32_t length = length_value->Int32Value();
    Handle<Array> result(Array::New(length > 0 ? length : 0));
    for (int32_t index = 0; index < length; ++index) {
        Handle<Integer> index_value(Integer::New(index));
        Handle<v8::Value> item(object->Get(index_value));
        if (item.IsEmpty())
            return Handle<Array>();
        result->Set(index_value, item);
    }
    return result;
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, ProxyBg, GetIndexedCb,
                    uint32_t, index,
                    const AccessorInfo&, /*info*/) const
{
    Handle<v8::Value> arg(Integer::New(index));
    return Call("get", 1, &arg);
}


DEFINE_JS_CALLBACK3(Handle<v8::Value>, ProxyBg, SetIndexedCb,
                    uint32_t, index,
                    Local<v8::Value>, value,
                    const AccessorInfo&, /*info*/) const
{
    Handle<v8::Value> argv[] = {Integer::New(index), value};
    return Call("set", 2, argv);
}


DEFINE_JS_CALLBACK2(Handle<Boolean>, ProxyBg, QueryIndexedCb,
                    uint32_t, index,
                    const AccessorInfo&, /*info*/) const
{
    return CallIndicator("query", Integer::New(index));
}


DEFINE_JS_CALLBACK2(Handle<Boolean>, ProxyBg, DeleteIndexedCb,
                    uint32_t, index,
                    const AccessorInfo&, /*info*/) const
{
    return CallIndicator("del", Integer::New(index));
}

////////////////////////////////////////////////////////////////////////////////
// HTTPParserBg
////////////////////////////////////////////////////////////////////////////////

#define DEFINE_HTTP_CALLBACK(name)                                  \
    static int On##name(http_parser *p) {                           \
        HTTPParserBg* parser = static_cast<HTTPParserBg*>(p->data); \
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
        HTTPParserBg* parser = static_cast<HTTPParserBg*>(p->data);     \
        AK_ASSERT(parser->binary_ptr_);                                 \
        Handle<v8::Value> value(Get(parser->handler_, "on" #name));     \
        if (!value->IsFunction())                                       \
            return 0;                                                   \
        Handle<Function> func(Handle<Function>::Cast(value));           \
        size_t start = at - parser->binary_ptr_->GetData();             \
        Handle<v8::Value> binary(                                       \
            BinaryBg::New(*parser->binary_ptr_, start, start + size));  \
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
    class HTTPParserBg {
    public:
        DECLARE_JS_CLASS(HTTPParserBg);

        HTTPParserBg(http_parser_type type, Handle<Object> handler);
        ~HTTPParserBg();

    private:
        static Handle<v8::Value> ConstructorCb(const Arguments& args);

        DEFINE_HTTP_CALLBACK(MessageBegin)
        DEFINE_HTTP_CALLBACK(MessageComplete)

        DEFINE_HTTP_DATA_CALLBACK(Path)
        DEFINE_HTTP_DATA_CALLBACK(Url)
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


DEFINE_JS_CONSTRUCTOR(HTTPParserBg, "HTTPParser", ConstructorCb,
                      /*object_template*/, proto_template)
{
    SetFunction(proto_template, "_exec", ExecCb);
}


Handle<v8::Value> HTTPParserBg::ConstructorCb(const Arguments& args)
{
    if (!args.IsConstructCall())
        return Undefined();
    try {
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
        HTTPParserBg::GetJSClass().Attach(
            args.This(), new HTTPParserBg(type, args[1]->ToObject()));
        return Handle<v8::Value>();
    } JS_CATCH(Handle<v8::Value>);
}


HTTPParserBg::HTTPParserBg(http_parser_type type, Handle<Object> handler)
    : handler_(Persistent<Object>::New(handler))
{
    http_parser_init(&impl_, type);
    impl_.data = this;
}


HTTPParserBg::~HTTPParserBg()
{
    handler_.Dispose();
}


string HTTPParserBg::GetMethodName(unsigned char method)
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


int HTTPParserBg::OnHeadersComplete(http_parser* p)
{
    HTTPParserBg* parser = static_cast<HTTPParserBg*>(p->data);
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


http_parser_settings HTTPParserBg::CreateSettings()
{
    http_parser_settings settings;
    settings.on_message_begin    = OnMessageBegin;
    settings.on_message_complete = OnMessageComplete;
    settings.on_path             = OnPath;
    settings.on_url              = OnUrl;
    settings.on_fragment         = OnFragment;
    settings.on_query_string     = OnQueryString;
    settings.on_header_field     = OnHeaderField;
    settings.on_header_value     = OnHeaderValue;
    settings.on_body             = OnBody;
    settings.on_headers_complete = OnHeadersComplete;
    return settings;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, HTTPParserBg, ExecCb,
                    const Arguments&, args)
{
    static http_parser_settings settings(CreateSettings());
    CheckArgsLength(args, 1);
    binary_ptr_ = BinaryBg::GetJSClass().Cast(args[0]);
    if (!binary_ptr_)
        throw Error(Error::TYPE, "Binary required");
    got_exception_ = false;
    const char* data = binary_ptr_->GetData();
    size_t size = binary_ptr_->GetSize();
    size_t parsed = http_parser_execute(&impl_, &settings, data, size);
    binary_ptr_ = 0;
    if (got_exception_)
        return Handle<v8::Value>();
    if (parsed != size)
        throw Error(Error::VALUE, "Parse error");
    return Undefined();
}

////////////////////////////////////////////////////////////////////////////////
// CoreBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class CoreBg {
    public:
        DECLARE_JS_CLASS(CoreBg);

        CoreBg(const Place& place,
               const CodeReader& code_reader,
               FSBg& fs_bg);

        void Init(Handle<Object> object) const;

    private:
        Place place_;
        const CodeReader& code_reader_;
        FSBg& fs_bg_;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, PrintCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, SetCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, ReadCodeCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, GetCodeModDateCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, HashCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, ConstructCb,
                             const Arguments&) const;
    };
}


DEFINE_JS_CLASS(CoreBg, "Core", object_template, /*proto_template*/)
{
    ScriptBg::GetJSClass();
    ProxyBg::GetJSClass();
    HTTPParserBg::GetJSClass();
    SetFunction(object_template, "print", PrintCb);
    SetFunction(object_template, "set", SetCb);
    SetFunction(object_template, "readCode", ReadCodeCb);
    SetFunction(object_template, "getCodeModDate", GetCodeModDateCb);
    SetFunction(object_template, "hash", HashCb);
    SetFunction(object_template, "construct", ConstructCb);
    Set(object_template, "db", DBBg::GetJSClass().GetObjectTemplate());
    Set(object_template, "fs", FSBg::GetJSClass().GetObjectTemplate());
}


CoreBg::CoreBg(const Place& place,
               const CodeReader& code_reader,
               FSBg& fs_bg)
    : place_(place)
    , code_reader_(code_reader)
    , fs_bg_(fs_bg)
{
}


void CoreBg::Init(Handle<Object> core) const
{
    JSClassBase::InitConstructors(core);
    Set(core, "app", String::New(place_.app_name.c_str()));
    if (!place_.spot_name.empty()) {
        Set(core, "spot", String::New(place_.spot_name.c_str()));
        Set(core, "owner", String::New(place_.owner_name.c_str()));
    }
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, CoreBg, PrintCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    Log(Stringify(args[0]));
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, CoreBg, SetCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 4);
    if (!args[0]->IsObject())
        throw Error(Error::TYPE, "Can't set property of non-object");
    Handle<Object> object(args[0]->ToObject());
    if (!args[2]->IsInt32())
        throw Error(Error::TYPE, "Property attribute must be integer");
    int32_t attributes = args[2]->Int32Value();
    if (attributes < 0 || attributes >= 8)
        throw Error(Error::VALUE,
                    ("Property attribute must be a "
                     "unsigned integer less than 8"));
    object->Set(args[1], args[3], static_cast<PropertyAttribute>(attributes));
    return object;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, CoreBg, ReadCodeCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    auto_ptr<Chars> data_ptr(args.Length() == 1
                             ? code_reader_.Read(Stringify(args[0]))
                             : code_reader_.Read(Stringify(args[0]),
                                                 Stringify(args[1])));
    return String::New(&data_ptr->front(), data_ptr->size());
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, CoreBg, GetCodeModDateCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    time_t date = (args.Length() == 1
                   ? code_reader_.GetModDate(Stringify(args[0]))
                   : code_reader_.GetModDate(Stringify(args[0]),
                                             Stringify(args[1])));
    return Date::New(static_cast<double>(date) * 1000);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, CoreBg, HashCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    int hash = (args[0]->IsObject()
                ? args[0]->ToObject()->GetIdentityHash()
                : 0);
    return Integer::New(hash);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, CoreBg, ConstructCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 2);
    if (!args[0]->IsFunction ())
        throw Error(Error::TYPE, "First argument must be function");
    Handle<Function> constructor(Handle<Function>::Cast(args[0]));
    Handle<Array> array(GetArray(args[1]));
    vector<Handle<v8::Value> > values;
    values.reserve(array->Length());
    for (size_t i = 0; i < array->Length(); ++i)
        values.push_back(array->Get(Integer::New(i)));
    return constructor->NewInstance(array->Length(), &values[0]);
}

////////////////////////////////////////////////////////////////////////////////
// GlobalBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class GlobalBg {
    public:
        DECLARE_JS_CLASS(GlobalBg);
        GlobalBg() {}
    };
}


DEFINE_JS_CLASS(GlobalBg, "Global", object_template, /*proto_template*/)
{
    Set(object_template, "_core",
        CoreBg::GetJSClass().GetObjectTemplate(),
        ReadOnly | DontEnum | DontDelete);
}

////////////////////////////////////////////////////////////////////////////////
// Utils
////////////////////////////////////////////////////////////////////////////////

namespace
{
    // Uses the address of a local variable to determine the stack top now.
    // Given a size, returns an address that is that far from the current
    // top of stack.
    // Taken from v8/test/cctest/test-api.cc
    static uint32_t* ComputeStackLimit(uint32_t size) {
        uint32_t* answer = &size - (size / sizeof(size));
        // If the size is very large and the stack is very near the bottom of
        // memory then the calculation above may wrap around and give an address
        // that is above the (downwards-growing) stack.  In that case we return
        // a very low address.
        if (answer > &size) return reinterpret_cast<uint32_t*>(sizeof(size));
        return answer;
    }


    jmp_buf environment;


    void HandleFatalError(const char* location, const char* message)
    {
        if (string(message) != "Allocation failed - process out of memory")
            Fail(string("Fatal error in ") + location + ": " + message);
        longjmp(environment, 1);
    }


    void Write(int fd, const char* data, size_t size)
    {
        if (fd == -1)
            return;
        while (size) {
            ssize_t count = write(fd, data, size);
            if (count <= 0)
                break;
            data += count;
            size -= count;
        }
    }


    void Write(int fd, const std::string& str)
    {
        Write(fd, str.c_str(), str.size());
    }
}

////////////////////////////////////////////////////////////////////////////////
// Program::Impl
////////////////////////////////////////////////////////////////////////////////

class Program::Impl {
public:
    Impl(const Place& place,
         const string& app_code_path,
         const string& release_code_path,
         const string& app_media_path,
         const string& release_media_path,
         DB& db);

    ~Impl();

    void Process(int sock_fd);
    void Eval(const Chars& expr, int out_fd);
    bool IsDead() const;

private:
    bool initialized_;
    DB& db_;
    CodeReader code_reader_;
    DBBg db_bg_;
    FSBg fs_bg_;
    CoreBg core_bg_;
    GlobalBg global_bg_;
    Persistent<Context> context_;
    Persistent<Object> core_;

    bool Run(Handle<Object> object,
             Handle<Function> function,
             Handle<v8::Value> arg,
             int out_fd,
             bool print_ok);

    void Call(Handle<Object> object,
              const string& func_name,
              Handle<v8::Value> arg,
              int out_fd = -1);

    void SetInternal(Handle<Object> object,
                     const string& field,
                     void* ptr) const;
};


Program::Impl::Impl(const Place& place,
                    const string& app_code_path,
                    const string& release_code_path,
                    const string& app_media_path,
                    const string& release_media_path,
                    DB& db)
    : initialized_(false)
    , db_(db)
    , code_reader_(app_code_path, release_code_path)
    , db_bg_(place.app_name == "profile")
    , fs_bg_(app_media_path, release_media_path, db.GetFSQuota())
    , core_bg_(place, code_reader_, fs_bg_)
{
    V8::SetFatalErrorHandler(HandleFatalError);

    ResourceConstraints rc;
    rc.set_max_young_space_size(MAX_YOUNG_SPACE_SIZE);
    rc.set_max_old_space_size(MAX_OLD_SPACE_SIZE);
    rc.set_stack_limit(ComputeStackLimit(STACK_LIMIT));
    bool ret = v8::SetResourceConstraints(&rc);
    AK_ASSERT(ret);

    HandleScope handle_scope;
    context_ = Context::New(NULL, GlobalBg::GetJSClass().GetObjectTemplate());
    Handle<Object> global_proto(context_->Global()->GetPrototype()->ToObject());
    global_proto->SetInternalField(0, External::New(&global_bg_));
    SetInternal(global_proto, "_core", &core_bg_);
    core_ = Persistent<Object>::New(Get(global_proto, "_core")->ToObject());
    SetInternal(core_, "db", &db_bg_);
    SetInternal(core_, "fs", &fs_bg_);

    Context::Scope context_scope(context_);
    core_bg_.Init(core_);
    db_bg_.Init(Get(core_, "db")->ToObject());
    Set(core_, "dbQuota", Number::New(db_.GetDBQuota()));
    Set(core_, "fsQuota", Number::New(db_.GetFSQuota()));

    Handle<Object> json(Get(context_->Global(), "JSON")->ToObject());
    stringify_json_func = Persistent<Function>::New(
        Handle<Function>::Cast(Get(json, "stringify")));
    parse_json_func = Persistent<Function>::New(
        Handle<Function>::Cast(Get(json, "parse")));

    // Run init.js script
    Handle<Script> script(Script::Compile(String::New(INIT_JS,
                                                      sizeof(INIT_JS)),
                                          String::New("native init.js")));
    AK_ASSERT(!script.IsEmpty());
    Handle<v8::Value> init_ret(script->Run());
    AK_ASSERT(!init_ret.IsEmpty());

    js_error_classes = Persistent<Object>::New(
        Get(core_, "errors")->ToObject()->Clone());
}


Program::Impl::~Impl()
{
    core_.Dispose();
    context_.Dispose();
}


void Program::Impl::Eval(const Chars& expr, int out_fd)
{
    HandleScope handle_scope;
    Context::Scope context_scope(context_);
    Handle<v8::Value> arg(String::New(&expr[0], expr.size()));
    Call(context_->Global(), "eval", arg, out_fd);
}


void Program::Impl::Process(int sock_fd)
{
    HandleScope handle_scope;
    Context::Scope context_scope(context_);
    SocketBg* socket_ptr(new SocketBg(sock_fd));
    Handle<v8::Value> arg(SocketBg::GetJSClass().Instantiate(socket_ptr));
    Call(core_, "main", arg);
    socket_ptr->Close();
}


bool Program::Impl::IsDead() const
{
    return V8::IsDead();
}


bool Program::Impl::Run(Handle<Object> object,
                        Handle<Function> function,
                        Handle<v8::Value> arg,
                        int out_fd,
                        bool print_ok)
{
    if (setjmp(environment)) {
        Write(out_fd, "ERROR\n<Out of memory>");
        return false;
    }
    Access access(db_);
    access_ptr = &access;
    TryCatch try_catch;
    Handle<v8::Value> value;
    {
        Watcher::ExecutionGuard guard;
        value = function->Call(object, 1, &arg);
    }
    access_ptr = 0;
    if (Watcher::TimedOut()) {
        Write(out_fd, "ERROR\n<Timed out>");
        return false;
    }
    if (try_catch.HasCaught() || value.IsEmpty()) {
        // I don't know the difference in these conditions but together
        // they handle all cases
        if (out_fd != -1) {
            ostringstream oss;
            oss << "ERROR\n";
            Handle<v8::Value> stack_trace(try_catch.StackTrace());
            if (!stack_trace.IsEmpty()) {
                oss << Stringify(stack_trace);
            } else {
                Handle<Message> message(try_catch.Message());
                if (!message.IsEmpty()) {
                    oss << Stringify(message->Get()) << "\n    at ";
                    Handle<v8::Value> resource_name(
                        message->GetScriptResourceName());
                    if (!resource_name->IsUndefined())
                        oss << Stringify(resource_name) << ':';
                    oss << message->GetLineNumber() << ':'
                        << message->GetStartColumn();
                } else {
                    Handle<v8::Value> exception(try_catch.Exception());
                    AK_ASSERT(!exception.IsEmpty());
                    oss << Stringify(exception);
                }
            }
            Write(out_fd, oss.str());
        }
        return false;
    }
    if (out_fd != -1 && print_ok) {
        Write(out_fd, "OK\n");
        Binarizator binarizator(value);
        Write(out_fd, binarizator.GetData(), binarizator.GetSize());
    }
    if (!db_bg_.WasRolledBack())
        access.Commit();
    return true;
}


void Program::Impl::Call(Handle<Object> object,
                         const string& func_name,
                         Handle<v8::Value> arg,
                         int out_fd)
{
    if (!initialized_) {
        Handle<Function> require_func(
            Handle<Function>::Cast(Get(context_->Global(), "require")));
        AK_ASSERT(!require_func.IsEmpty());
        if (!Run(context_->Global(), require_func, String::New("main"),
                 out_fd, false))
            return;
        initialized_ = true;
    }
    Handle<v8::Value> func_value(Get(object, func_name));
    if (func_value.IsEmpty() || !func_value->IsFunction())
        Write(out_fd, "ERROR\n" + func_name + " is not a function");
    else
        Run(object, Handle<Function>::Cast(func_value), arg, out_fd, true);
}


void Program::Impl::SetInternal(Handle<Object> object,
                                const string& field,
                                void* ptr) const
{
    Get(object, field)->ToObject()->SetInternalField(0, External::New(ptr));
}

////////////////////////////////////////////////////////////////////////////////
// Program
////////////////////////////////////////////////////////////////////////////////

Program::Program(const Place& place,
                 const string& app_code_path,
                 const string& release_code_path,
                 const string& app_media_path,
                 const string& release_media_path,
                 DB& db)
    : pimpl_(new Impl(place,
                      app_code_path,
                      release_code_path,
                      app_media_path,
                      release_media_path,
                      db))
{
}


Program::~Program()
{
}


void Program::Process(int fd)
{
    return pimpl_->Process(fd);
}


void Program::Eval(const Chars& expr, int fd)
{
    return pimpl_->Eval(expr, fd);
}


bool Program::IsDead() const
{
    return pimpl_->IsDead();
}
