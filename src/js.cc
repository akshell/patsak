
// (c) 2009-2010 by Anton Korenyushkin

#include "js.h"
#include "js-db.h"
#include "js-file.h"
#include "db.h"

#include <boost/foreach.hpp>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <asio.hpp>

#include <setjmp.h>


using namespace ku;
using namespace std;
using namespace v8;
using boost::scoped_ptr;
using boost::bind;
using boost::noncopyable;
using asio::ip::tcp;


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
        CodeReader(const string& code_path, const string& include_path);
        auto_ptr<Chars> Read(const string& path) const;
        auto_ptr<Chars> Read(const string& app_name, const string& path) const;
        time_t GetModDate(const string& path) const;
        time_t GetModDate(const string& app_name, const string& path) const;

    private:
        string code_path_;
        string include_path_;

        void CheckPath(const string& path) const;
        
        auto_ptr<Chars> DoRead(const string& base_path,
                               const string& path) const;
        
        time_t DoGetModDate(const string& base_path, const string& path) const;
    };
}


CodeReader::CodeReader(const string& code_path, const string& include_path)
    : code_path_(code_path), include_path_(include_path)
{
}


auto_ptr<Chars> CodeReader::Read(const string& path) const
{
    return DoRead(code_path_, path);
}


auto_ptr<Chars> CodeReader::Read(const string& app_name, const string& path) const
{
    access_ptr->CheckAppExists(app_name);
    return DoRead(include_path_ + '/' + app_name, path);
}


time_t CodeReader::GetModDate(const string& path) const
{
    return DoGetModDate(code_path_, path);
}


time_t CodeReader::GetModDate(const string& app_name, const string& path) const
{
    access_ptr->CheckAppExists(app_name);
    return DoGetModDate(include_path_ + '/' + app_name, path);
}


void CodeReader::CheckPath(const string& path) const
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


JSClass<ScriptBg>& ScriptBg::GetJSClass()
{
    static JSClass<ScriptBg> result("Script", ConstructorCb);
    return result;
}


void ScriptBg::AdjustTemplates(Handle<ObjectTemplate> /*object_template*/,
                               Handle<ObjectTemplate> proto_template)
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


JSClass<ProxyBg>& ProxyBg::GetJSClass()
{
    static JSClass<ProxyBg> result("Proxy", ConstructorCb);
    return result;
}


void ProxyBg::AdjustTemplates(Handle<ObjectTemplate> object_template,
                              Handle<ObjectTemplate> /*proto_template*/)
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
// CoreBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class CoreBg {
    public:
        DECLARE_JS_CLASS(CoreBg);

        CoreBg(const Place& place,
               const CodeReader& code_reader,
               AppAccessor& app_accessor,
               FSBg& fs_bg);

        void Init(Handle<Object> object) const;
        
    private:
        Place place_;
        const CodeReader& code_reader_;
        AppAccessor& app_accessor_;
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
        
        DECLARE_JS_CALLBACK1(Handle<v8::Value>, RequestAppCb,
                             const Arguments&) const;
        
        DECLARE_JS_CALLBACK1(Handle<v8::Value>, RequestHostCb,
                             const Arguments&) const;
    };
}


DEFINE_JS_CLASS(CoreBg, "Core", object_template, /*proto_template*/)
{
    ScriptBg::GetJSClass();
    ProxyBg::GetJSClass();
    SetFunction(object_template, "print", PrintCb);
    SetFunction(object_template, "set", SetCb);
    SetFunction(object_template, "readCode", ReadCodeCb);
    SetFunction(object_template, "getCodeModDate", GetCodeModDateCb);
    SetFunction(object_template, "hash", HashCb);
    SetFunction(object_template, "construct", ConstructCb);
    SetFunction(object_template, "requestApp", RequestAppCb);
    SetFunction(object_template, "requestHost", RequestHostCb);
    Set(object_template, "db", DBBg::GetJSClass().GetObjectTemplate());
    Set(object_template, "fs", FSBg::GetJSClass().GetObjectTemplate());
}


CoreBg::CoreBg(const Place& place,
               const CodeReader& code_reader,
               AppAccessor& app_accessor,
               FSBg& fs_bg)
    : place_(place)
    , code_reader_(code_reader)
    , app_accessor_(app_accessor)
    , fs_bg_(fs_bg)
{
}


void CoreBg::Init(Handle<Object> core) const
{
    JSClassBase::InitConstructors(core);
    Handle<Object> app(Object::New());
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
        throw Error(Error::USAGE,
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
        throw Error(Error::USAGE, "First argument must be function");
    Handle<Function> constructor(Handle<Function>::Cast(args[0]));
    Handle<Array> array(GetArray(args[1]));
    vector<Handle<v8::Value> > values;
    values.reserve(array->Length());
    for (size_t i = 0; i < array->Length(); ++i)
        values.push_back(array->Get(Integer::New(i)));
    return constructor->NewInstance(array->Length(), &values[0]);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, CoreBg, RequestAppCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 4);
    string app_name(Stringify(args[0]));
    string request(Stringify(args[1]));
    BinaryBg::Reader binary_reader(args[3]);

    Handle<Array> files(GetArray(args[2]));
    Strings file_pathes;
    file_pathes.reserve(files->Length());
    for (size_t i = 0; i < files->Length(); ++i) {
        Handle<v8::Value> item(files->Get(Integer::New(i)));
        const FileBg* file_ptr = FileBg::GetJSClass().Cast(item);
        string path(file_ptr ? file_ptr->GetPath() : fs_bg_.ReadPath(item));
        if (!S_ISREG(GetStat(path)->st_mode))
            throw Error(Error::ENTRY_IS_DIR, "Directory cannot be passed");
        file_pathes.push_back(path);
    }

    auto_ptr<Chars> data_ptr(app_accessor_(app_name,
                                           request,
                                           file_pathes,
                                           binary_reader.GetStartPtr(),
                                           binary_reader.GetSize(),
                                           *access_ptr));
    KU_ASSERT(data_ptr->size() >= 3);
    if (string(&data_ptr->front(), 3) == "OK\n") {
        data_ptr->erase(data_ptr->begin(), data_ptr->begin() + 3);
        return JSNew<BinaryBg>(data_ptr);
    } else {
        KU_ASSERT_MESSAGE(string(&data_ptr->front(), 6) == "ERROR\n",
                          string(&data_ptr->front(), data_ptr->size()));
        throw Error(Error::REQUEST_APP,
                    "Exception occured in \"" + app_name + "\" app");
    }
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, CoreBg, RequestHostCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 3);
    asio::io_service io_service;
    tcp::resolver resolver(io_service);
    tcp::resolver::query query(Stringify(args[0]), Stringify(args[1]));
    tcp::socket socket(io_service);
    try {
        asio::error_code error_code = asio::error::host_not_found;
        for (tcp::resolver::iterator itr = resolver.resolve(query);
             itr != tcp::resolver::iterator();
             ++itr)
            if (!socket.connect(*itr, error_code))
                break;
        if (error_code)
            throw asio::system_error(error_code);
        BinaryBg::Reader binary_reader(args[2]);
        if (binary_reader.GetSize())
            asio::write(socket,
                        asio::buffer(binary_reader.GetStartPtr(),
                                     binary_reader.GetSize()),
                        asio::transfer_all());
        asio::streambuf streambuf;
        asio::read(socket, streambuf, asio::transfer_all(), error_code);
        if (error_code != asio::error::eof)
            throw asio::system_error(error_code);
        const char* data_ptr = asio::buffer_cast<const char*>(streambuf.data());
        return JSNew<BinaryBg>(
            auto_ptr<Chars>(new Chars(data_ptr, data_ptr + streambuf.size())));
    } catch (const asio::system_error& error) {
        throw Error(Error::REQUEST_HOST, error.what());
    }
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
// OkResponse
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class OkResponse : public Response {
    public:
        OkResponse(Handle<v8::Value> value);
        virtual string GetStatus() const;
        virtual size_t GetSize() const;
        virtual const char* GetData() const;

    private:
        BinaryBg::Reader binary_reader_;
    };  
}


OkResponse::OkResponse(Handle<v8::Value> value)
    : binary_reader_(value)
{
}


string OkResponse::GetStatus() const
{
    return "OK";
}


size_t OkResponse::GetSize() const
{
    return binary_reader_.GetSize();
}


const char* OkResponse::GetData() const
{
    return binary_reader_.GetStartPtr();
}

////////////////////////////////////////////////////////////////////////////////
// ErrorResponse
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class ErrorResponse : public Response {
    public:
        ErrorResponse(const string& descr);

        virtual string GetStatus() const;
        virtual size_t GetSize() const;
        virtual const char* GetData() const;

    private:
        string descr_;
    };
}


ErrorResponse::ErrorResponse(const string& descr)
    : descr_(descr)
{
}


string ErrorResponse::GetStatus() const
{
    return "ERROR";
}


size_t ErrorResponse::GetSize() const
{
    return descr_.size();
}


const char* ErrorResponse::GetData() const
{
    return descr_.data();
}

////////////////////////////////////////////////////////////////////////////////
// ExceptionResponse
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class ExceptionResponse : public ErrorResponse {
    public:
        ExceptionResponse(const TryCatch& try_catch);
        
    private:
        static string MakeExceptionDescr(const TryCatch& try_catch);
    };
}


ExceptionResponse::ExceptionResponse(const TryCatch& try_catch)
    : ErrorResponse(MakeExceptionDescr(try_catch))
{
}


string ExceptionResponse::MakeExceptionDescr(const TryCatch& try_catch)
{
    ostringstream oss;
    Handle<Message> message(try_catch.Message());
    if (!message.IsEmpty()) {
        Handle<v8::Value> resource_name(message->GetScriptResourceName());
        if (!resource_name->IsUndefined())
            oss << "File \"" << Stringify(message->GetScriptResourceName())
                << "\", line ";
        else
            oss << "Line ";
        oss <<  message->GetLineNumber()
            << ", column " << message->GetStartColumn() << '\n';
    }
    Handle<v8::Value> stack_trace(try_catch.StackTrace());
    Handle<v8::Value> exception(try_catch.Exception());
    if (!stack_trace.IsEmpty())
        oss << Stringify(stack_trace);
    else if (!message.IsEmpty())
        oss << Stringify(message->Get());
    else if (!exception.IsEmpty())
        oss << Stringify(exception);
    else
        oss << "<Unknown exception>";
    return oss.str();
}

////////////////////////////////////////////////////////////////////////////////
// ParseDate from common.h
////////////////////////////////////////////////////////////////////////////////

namespace
{
    Handle<Function> parse_date_func;
}


double ku::ParseDate(const string& str)
{
    KU_ASSERT(!parse_date_func.IsEmpty());
    Handle<v8::Value> value(String::New(str.c_str()));
    Handle<v8::Value> result(
        parse_date_func->Call(Context::GetCurrent()->Global(), 1, &value));
    KU_ASSERT(result->IsNumber());
    return result->NumberValue();
}

////////////////////////////////////////////////////////////////////////////////
// ComputeStackLimit
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
}

////////////////////////////////////////////////////////////////////////////////
// HandleFatalError
////////////////////////////////////////////////////////////////////////////////

namespace
{
    jmp_buf environment;

    
    void HandleFatalError(const char* location, const char* message)
    {
        if (string(message) != "Allocation failed - process out of memory")
            Fail(string("Fatal error in ") + location + ": " + message);
        longjmp(environment, 1);
    }
}


////////////////////////////////////////////////////////////////////////////////
// Program::Impl
////////////////////////////////////////////////////////////////////////////////

class Program::Impl {
public:
    Impl(const Place& place,
         const string& code_dir,
         const string& include_dir,
         const string& media_dir,
         DB& db,
         AppAccessor& app_accessor);
    
    ~Impl();
    
    auto_ptr<Response> Process(const string& user,
                               const Chars& request,
                               const Strings& pathes,
                               auto_ptr<Chars> data_ptr,
                               const string& issuer);

    auto_ptr<Response> Eval(const string& user, const Chars& expr);
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
    
    auto_ptr<Response> Run(Handle<Function> function,
                           Handle<Object> object,
                           Handle<v8::Value> arg);

    auto_ptr<Response> Call(const string& user,
                            const Chars& input,
                            const Strings& file_pathes,
                            auto_ptr<Chars> data_ptr,
                            const string& issuer,
                            Handle<Object> object,
                            const string& func_name);

    void SetInternal(Handle<Object> object,
                     const string& field,
                     void* ptr) const;
};


Program::Impl::Impl(const Place& place,
                    const string& code_dir,
                    const string& include_dir,
                    const string& media_dir,
                    DB& db,
                    AppAccessor& app_accessor)
    : initialized_(false)
    , db_(db)
    , code_reader_(code_dir, include_dir)
    , fs_bg_(media_dir, db.GetFSQuota())
    , core_bg_(place, code_reader_, app_accessor, fs_bg_)
{
    V8::SetFatalErrorHandler(HandleFatalError);
    
    ResourceConstraints rc;
    rc.set_max_young_space_size(MAX_YOUNG_SPACE_SIZE);
    rc.set_max_old_space_size(MAX_OLD_SPACE_SIZE);
    rc.set_stack_limit(ComputeStackLimit(STACK_LIMIT));
    bool ret = v8::SetResourceConstraints(&rc);
    KU_ASSERT(ret);
    
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

    parse_date_func = Persistent<Function>::New(
        Handle<Function>::Cast(
            Get(Get(context_->Global(), "Date")->ToObject(), "parse")));
    
    // Run init.js script
    Handle<Script> script(Script::Compile(String::New(INIT_JS,
                                                      sizeof(INIT_JS)),
                                          String::New("native init.js")));
    KU_ASSERT(!script.IsEmpty());
    Handle<v8::Value> init_ret(script->Run());
    KU_ASSERT(!init_ret.IsEmpty());

    js_error_classes = Persistent<Object>::New(
        Get(core_, "errors")->ToObject()->Clone());
}


Program::Impl::~Impl()
{
    core_.Dispose();
    context_.Dispose();
}


auto_ptr<Response> Program::Impl::Process(const string& user,
                                          const Chars& request,
                                          const Strings& file_pathes,
                                          auto_ptr<Chars> data_ptr,
                                          const string& issuer)
{
    HandleScope handle_scope;
    return Call(user, request,
                file_pathes, data_ptr, issuer,
                core_, "main");
}


auto_ptr<Response> Program::Impl::Eval(const string& user, const Chars& expr)
{
    HandleScope handle_scope;
    return Call(user, expr,
                Strings(), auto_ptr<Chars>(), "",
                context_->Global(), "eval");
}


bool Program::Impl::IsDead() const
{
    return V8::IsDead();
}


auto_ptr<Response> Program::Impl::Run(Handle<Function> function,
                                      Handle<Object> object,
                                      Handle<v8::Value> arg)
{
    if (setjmp(environment))
        return auto_ptr<Response>(new ErrorResponse("<Out of memory>"));
    Access access(db_);
    access_ptr = &access;
    TryCatch try_catch;
    Handle<v8::Value> value;
    {
        Watcher::ExecutionGuard guard;
        value = function->Call(object, 1, &arg);
    }
    auto_ptr<Response> result;
    if (Watcher::TimedOut()) {
        result.reset(new ErrorResponse("<Timed out>"));
    } else if (try_catch.HasCaught() || value.IsEmpty()) {
        // I don't know the difference in these conditions but together
        // they handle all cases
        result.reset(new ExceptionResponse(try_catch));
    } else {
        result.reset(new OkResponse(value));
        if (!db_bg_.WasRolledBack())
            access.Commit();
    }
    access_ptr = 0;
    return result;
}
    

auto_ptr<Response> Program::Impl::Call(const string& user,
                                       const Chars& input,
                                       const Strings& file_pathes,
                                       auto_ptr<Chars> data_ptr,
                                       const string& issuer,
                                       Handle<Object> object,
                                       const string& func_name)
{
    Context::Scope context_scope(context_);
    if (!initialized_) {
        Handle<Function> require_func(
            Handle<Function>::Cast(Get(context_->Global(), "require")));
        KU_ASSERT(!require_func.IsEmpty());
        auto_ptr<Response> response_ptr(
            Run(require_func, context_->Global(), String::New("main")));
        if (response_ptr->GetStatus() != "OK")
            return response_ptr;
        initialized_ = true;
    }
    
    Handle<v8::Value> func_value(Get(object, func_name));
    if (func_value.IsEmpty() || !func_value->IsFunction())
        return auto_ptr<Response>(
            new ErrorResponse(func_name + " is not a function"));
    
    Handle<Array> files(Array::New(file_pathes.size()));
    for (size_t i = 0; i < file_pathes.size(); ++i)
        files->Set(Integer::New(i), JSNew<FileBg>(file_pathes[i]));
    Set(core_, "files", files);
    Set(core_, "data", JSNew<BinaryBg>(data_ptr));
    Set(core_, "user", String::New(user.c_str()));
    Set(core_, "issuer", String::New(issuer.c_str()));

    return Run(Handle<Function>::Cast(func_value),
               object,
               String::New(&input[0], input.size()));
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
                 const string& code_dir,
                 const string& include_dir,
                 const string& media_dir,
                 DB& db,
                 AppAccessor& app_accessor)
    : pimpl_(new Impl(place,
                      code_dir,
                      include_dir,
                      media_dir,
                      db,
                      app_accessor))
{
}


Program::~Program()
{
}


auto_ptr<Response> Program::Process(const string& user,
                                    const Chars& request,
                                    const Strings& file_pathes,
                                    auto_ptr<Chars> data_ptr,
                                    const string& issuer)
{
    return pimpl_->Process(user, request, file_pathes, data_ptr, issuer);
}


auto_ptr<Response> Program::Eval(const string& user, const Chars& expr)
{
    return pimpl_->Eval(user, expr);
}


bool Program::IsDead() const
{
    return pimpl_->IsDead();
}
