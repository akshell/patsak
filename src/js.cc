
// (c) 2009-2010 by Anton Korenyushkin

/// \file js.h
/// JavaScript interpreter impl

#include "js.h"
#include "js-db.h"
#include "js-file.h"
#include "db.h"

#include <boost/foreach.hpp>
#include <boost/thread.hpp>
#include <boost/bind.hpp>

#include <sstream>


using namespace ku;
using namespace std;
using namespace v8;
using boost::scoped_ptr;
using boost::bind;
using boost::noncopyable;


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
    /// Class for read access to code files of current and other applications
    class CodeReader {
    public:
        CodeReader(const string& code_path, const string& include_path);
        Chars operator()(const string& path) const;
        Chars operator()(const string& app_name, const string& path) const;
        
    private:
        string code_path_;
        string include_path_;

        Chars Read(const string& base_path, const string& path) const;
    };
}


CodeReader::CodeReader(const string& code_path, const string& include_path)
    : code_path_(code_path), include_path_(include_path)
{
}


Chars CodeReader::operator()(const string& path) const
{
    return Read(code_path_, path);
}


Chars CodeReader::operator()(const string& app_name, const string& path) const
{
    access_ptr->CheckAppExists(app_name);
    return Read(include_path_ + '/' + app_name, path);
}


Chars CodeReader::Read(const string& base_path, const string& path) const
{
    if (GetPathDepth(path) <= 0)
        throw Error(Error::PATH, "Code path \"" + path + "\" is illegal");
    return ReadFileData(base_path + '/' + path);
}

////////////////////////////////////////////////////////////////////////////////
// ScriptBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Script background
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


JSClass<ScriptBg>& ScriptBg::GetJSClass() {
    static JSClass<ScriptBg> result("Script", ConstructorCb);
    return result;
}


void ScriptBg::AdjustTemplates(Handle<ObjectTemplate> /*object_template*/,
                               Handle<ObjectTemplate> proto_template)
{
    SetFunction(proto_template, "run", RunCb);
}


Handle<v8::Value> ScriptBg::ConstructorCb(const Arguments& args)
{
    if (!args.IsConstructCall()) {
        vector<Handle<v8::Value> > arguments;
        arguments.reserve(args.Length());
        for (int i = 0; i < args.Length(); ++i)
            arguments.push_back(args[i]);
        return ScriptBg::GetJSClass().GetFunction()->NewInstance(args.Length(),
                                                                 &arguments[0]);
    }
    try {
        CheckArgsLength(args, 1);
        Handle<Script> script(
            args.Length() == 1
            ? Script::Compile(args[0]->ToString())
            : Script::Compile(args[0]->ToString(), args[1]));
        if (!script.IsEmpty())
            ScriptBg::GetJSClass().Attach(args.This(),
                                          new ScriptBg(script));
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
// AKBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// ak background
    class AKBg {
    public:
        DECLARE_JS_CLASS(AKBg);

        AKBg(const Place& place,
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
        
        DECLARE_JS_CALLBACK1(Handle<v8::Value>, SetObjectPropCb,
                             const Arguments&) const;
        
        DECLARE_JS_CALLBACK1(Handle<v8::Value>, ReadCodeCb,
                             const Arguments&) const;
        
        DECLARE_JS_CALLBACK1(Handle<v8::Value>, HashCb,
                             const Arguments&) const;
        
        DECLARE_JS_CALLBACK1(Handle<v8::Value>, ConstructCb,
                             const Arguments&) const;
        
        DECLARE_JS_CALLBACK1(Handle<v8::Value>, RequestAppCb,
                             const Arguments&) const;
    };
}


DEFINE_JS_CLASS(AKBg, "AK", object_template, proto_template)
{
    ScriptBg::GetJSClass();
    SetFunction(proto_template, "print", PrintCb);
    SetFunction(proto_template, "setObjectProp", SetObjectPropCb);
    SetFunction(proto_template, "readCode", ReadCodeCb);
    SetFunction(proto_template, "hash", HashCb);
    SetFunction(proto_template, "construct", ConstructCb);
    SetFunction(proto_template, "requestApp", RequestAppCb);
    Set(object_template, "db", DBBg::GetJSClass().GetObjectTemplate());
    Set(object_template, "fs", FSBg::GetJSClass().GetObjectTemplate());
}


AKBg::AKBg(const Place& place,
           const CodeReader& code_reader,
           AppAccessor& app_accessor,
           FSBg& fs_bg)
    : place_(place)
    , code_reader_(code_reader)
    , app_accessor_(app_accessor)
    , fs_bg_(fs_bg)
{
}


void AKBg::Init(Handle<Object> ak) const
{
    JSClassBase::InitConstructors(ak);
    Handle<Object> app(Object::New());
    Set(app, "name", String::New(place_.app_name.c_str()));
    if (!place_.spot_name.empty()) {
        Handle<Object> spot(Object::New());
        Set(spot, "name", String::New(place_.spot_name.c_str()));
        Set(spot, "owner", String::New(place_.owner_name.c_str()));
        Set(app, "spot", spot);
    }
    Set(ak, "app", app);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, AKBg, PrintCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    Log(Stringify(args[0]));
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, AKBg, SetObjectPropCb,
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
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, AKBg, ReadCodeCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    Chars data = (args.Length() == 1
                  ? code_reader_(Stringify(args[0]))
                  : code_reader_(Stringify(args[0]), Stringify(args[1])));
    return String::New(&data[0], data.size());
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, AKBg, HashCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    int hash = (args[0]->IsObject()
                ? args[0]->ToObject()->GetIdentityHash()
                : 0);
    return Integer::New(hash);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, AKBg, ConstructCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 2);
    if (!args[0]->IsFunction ())
        throw Error(Error::USAGE, "First argument must be function");
    Handle<Function> constructor(Handle<Function>::Cast(args[0]));
    size_t length = GetArrayLikeLength(args[1]);
    vector<Handle<v8::Value> > arguments;
    arguments.reserve(length);
    for (size_t i = 0; i < length; ++i)
        arguments.push_back(GetArrayLikeItem(args[1], i));
    return constructor->NewInstance(length, &arguments[0]);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, AKBg, RequestAppCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 4);
    string app_name(Stringify(args[0]));
    string request(Stringify(args[1]));
    
    size_t length = GetArrayLikeLength(args[2]);
    vector<Handle<v8::Value> > file_values;
    file_values.reserve(length);
    for (size_t i = 0; i < length; ++i)
        file_values.push_back(GetArrayLikeItem(args[2], i));
    FSBg::FileAccessor file_accessor(fs_bg_, file_values);
    
    const Chars* data_ptr = 0;
    Chars data;
    const DataBg* data_bg_ptr = DataBg::GetJSClass().Cast(args[3]);
    if (data_bg_ptr) {
        data_ptr = &data_bg_ptr->GetData();
    } else if (!args[3]->IsNull() && !args[3]->IsUndefined()) {
        String::Utf8Value utf8_value(args[3]);
        data.assign(*utf8_value, *utf8_value + utf8_value.length());
        data_ptr = &data;
    }
        
    Chars result(app_accessor_(app_name,
                               request,
                               file_accessor.GetFullPathes(),
                               data_ptr,
                               *access_ptr));
    KU_ASSERT(result.size() >= 3);
    if (string(&result[0], 3) == "OK\n") {
        return String::New(&result[3], result.size() - 3);
    } else {
        KU_ASSERT(string(&result[0], 6) == "ERROR\n");
        throw Error(Error::PROCESSING_FAILED,
                    "Exception occured in \"" + app_name + "\" app");
    }
}

////////////////////////////////////////////////////////////////////////////////
// GlobalBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Global background
    class GlobalBg {
    public:
        DECLARE_JS_CLASS(GlobalBg);
        GlobalBg() {}
    };    
}


DEFINE_JS_CLASS(GlobalBg, "Global", object_template, /*proto_template*/)
{
    Set(object_template, "ak",
        AKBg::GetJSClass().GetObjectTemplate(),
        ReadOnly | DontDelete);
}

////////////////////////////////////////////////////////////////////////////////
// OkResponse
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Result of a successful evaluation
    class OkResponse : public Response {
    public:
        OkResponse(Handle<v8::Value> value);
        virtual string GetStatus() const;
        virtual size_t GetSize() const;
        virtual const char* GetData() const;

    private:
        // mutable is due to the fact that String::Utf8Value::length()
        // isn't const for unknown reasons
        mutable String::Utf8Value utf8_value_;
    };  
}


OkResponse::OkResponse(Handle<v8::Value> value)
    : utf8_value_(value)
{
}


string OkResponse::GetStatus() const
{
    return "OK";
}


size_t OkResponse::GetSize() const
{
    return static_cast<size_t>(utf8_value_.length());
}


const char* OkResponse::GetData() const
{
    return *utf8_value_;
}

////////////////////////////////////////////////////////////////////////////////
// ErrorResponse
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Evaluation failure descriptor
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
    else if (Context::GetCurrent()->HasOutOfMemoryException())
        oss << "<Out of memory>";
    else
        oss << "<Unknown exception>";
    return oss.str();
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
    bool IsOperable() const;
    
private:
    bool initialized_;
    DB& db_;
    CodeReader code_reader_;
    DBBg db_bg_;
    FSBg fs_bg_;
    AKBg ak_bg_;
    GlobalBg global_bg_;
    Persistent<Context> context_;
    Persistent<Object> ak_;
    
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
    , ak_bg_(place, code_reader_, app_accessor, fs_bg_)
{
    ResourceConstraints rc;
    rc.set_max_young_space_size(MAX_YOUNG_SPACE_SIZE);
    rc.set_max_old_space_size(MAX_OLD_SPACE_SIZE);
    rc.set_stack_limit(ComputeStackLimit(STACK_LIMIT));
    bool ret = v8::SetResourceConstraints(&rc);
    KU_ASSERT(ret);
    
    HandleScope handle_scope;
    context_ = Context::New(NULL, GlobalBg::GetJSClass().GetObjectTemplate());
    V8::IgnoreOutOfMemoryException();
    Handle<Object> global_proto(context_->Global()->GetPrototype()->ToObject());
    global_proto->SetInternalField(0, External::New(&global_bg_));
    SetInternal(global_proto, "ak", &ak_bg_);
    ak_ = Persistent<Object>::New(Get(global_proto, "ak")->ToObject());
    SetInternal(ak_, "db", &db_bg_);
    SetInternal(ak_, "fs", &fs_bg_);

    Context::Scope context_scope(context_);
    ak_bg_.Init(ak_);
    db_bg_.Init(Get(ak_, "db")->ToObject());
    Set(ak_, "dbQuota", Number::New(db_.GetDBQuota()), DontEnum);
    Set(ak_, "fsQuota", Number::New(db_.GetFSQuota()), DontEnum);
    // Run init.js script
    Handle<Script> script(Script::Compile(String::New(INIT_JS,
                                                      sizeof(INIT_JS)),
                                          String::New("native init.js")));
    KU_ASSERT(!script.IsEmpty());
    Handle<v8::Value> init_ret(script->Run());
    KU_ASSERT(!init_ret.IsEmpty());
}


Program::Impl::~Impl()
{
    ak_.Dispose();
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
                ak_, "_main");
}


auto_ptr<Response> Program::Impl::Eval(const string& user, const Chars& expr)
{
    HandleScope handle_scope;
    return Call(user, expr,
                Strings(), auto_ptr<Chars>(), "",
                context_->Global(), "eval");
}


bool Program::Impl::IsOperable() const
{
    return !context_->HasOutOfMemoryException();
}


auto_ptr<Response> Program::Impl::Run(Handle<Function> function,
                                      Handle<Object> object,
                                      Handle<v8::Value> arg)
{
    Access access(db_);
    access_ptr = &access;
    TryCatch try_catch;
    Handle<v8::Value> result;
    {
        Watcher::ExecutionGuard guard;
        result = function->Call(object, 1, &arg);
    }
    access_ptr = 0;
    if (Watcher::TimedOut())
        return auto_ptr<Response>(new ErrorResponse("<Timed out>"));
    // I don't know the difference in these conditions but together
    // they handle all cases
    if (try_catch.HasCaught() || result.IsEmpty())
        return auto_ptr<Response>(new ExceptionResponse(try_catch));
    access.Commit();
    return auto_ptr<Response>(new OkResponse(result));
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
        Handle<Function> include_func(
            Handle<Function>::Cast(Get(ak_, "include")));
        KU_ASSERT(!include_func.IsEmpty());
        auto_ptr<Response> response_ptr(
            Run(include_func, ak_, String::New("__main__.js")));
        if (response_ptr->GetStatus() != "OK")
            return response_ptr;
        initialized_ = true;
    }
    
    Handle<v8::Value> func_value(Get(object, func_name));
    if (func_value.IsEmpty() || !func_value->IsFunction())
        return auto_ptr<Response>(
            new ErrorResponse('"' + func_name + "\" is not a function"));
    
    Handle<v8::Value> data_value;
    if (data_ptr.get())
        data_value = JSNew<DataBg>(data_ptr);
    else
        data_value = Null();
    Set(ak_, "_data", data_value, DontEnum);

    Handle<Array> file_array(Array::New(file_pathes.size()));
    vector<Handle<Object> > files;
    files.reserve(file_pathes.size());
    for (size_t i = 0; i < file_pathes.size(); ++i) {
        Handle<Object> file(JSNew<TempFileBg>(file_pathes[i]));
        files.push_back(file);
        file_array->Set(Integer::New(i), file);
    }
    Set(ak_, "_files", file_array, DontEnum);

    Set(ak_, "_user", String::New(user.c_str()), DontEnum);

    if (issuer.empty())
        ak_->Delete(String::New("_issuer"));
    else
        Set(ak_, "_issuer", String::New(issuer.c_str()), DontEnum);

    auto_ptr<Response> result(Run(Handle<Function>::Cast(func_value),
                                  object,
                                  String::New(&input[0], input.size())));
    
    BOOST_FOREACH(Handle<Object> file, files)
        TempFileBg::GetJSClass().Cast(file)->ClearPath();
    
    return result;    
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


bool Program::IsOperable() const
{
    return pimpl_->IsOperable();
}
