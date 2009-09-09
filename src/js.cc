
// (c) 2009 by Anton Korenyushkin

/// \file js.h
/// JavaScript interpreter impl

#include "js.h"
#include "js-common.h"
#include "js-db.h"
#include "js-file.h"
#include "db.h"

#include <boost/foreach.hpp>
#include <boost/utility.hpp>
#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/ref.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>
#include <vector>
#include <sstream>
#include <iostream>
#include <memory>


using namespace ku;
using namespace std;
using namespace v8;
using boost::noncopyable;
using boost::ptr_vector;
using boost::ref;


////////////////////////////////////////////////////////////////////////////////
// Constants
////////////////////////////////////////////////////////////////////////////////

// File generated from include.js script by xxd -i, INCLUDE_JS is defined here
#include "include.js.h"


namespace
{
    const int MAX_YOUNG_SPACE_SIZE =  2 * 1024 * 1024;
    const int MAX_OLD_SPACE_SIZE   = 32 * 1024 * 1024;
}

////////////////////////////////////////////////////////////////////////////////
// AppBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Application background
    class AppBg {
    public:
        DECLARE_JS_CLASS(AppBg);

        AppBg(AppAccessor& app_accessor,
              FSBg& fs_bg,
              const string& app_name);

    private:
        AppAccessor& app_accessor_;
        FSBg& fs_bg_;
        string app_name_;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, CallCb,
                             const Arguments&) const;
    };
}


DEFINE_JS_CLASS(AppBg, "App", /*object_template*/, proto_template)
{
    SetFunction(proto_template, "_call", CallCb);
}


AppBg::AppBg(AppAccessor& app_accessor,
             FSBg& fs_bg,
             const string& app_name)
    : app_accessor_(app_accessor)
    , fs_bg_(fs_bg)
    , app_name_(app_name)
{
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, AppBg, CallCb,
                    const Arguments&, args) const
{
    JS_CHECK(args.Length() > 0 && args.Length() < 4,
             "One, two or three arguments required");

    vector<Handle<v8::Value> > file_values;
    if (args.Length() > 1) {
        int32_t length = GetArrayLikeLength(args[1]);
        JS_TYPE_CHECK(length != -1, "Call file list must be array-like");
        file_values.reserve(length);
        for (int32_t i = 0; i < length; ++i) {
            file_values.push_back(GetArrayLikeItem(args[1], i));
            if (file_values.back().IsEmpty())
                return Handle<v8::Value>();
        }
    }
    FSBg::FileAccessor file_accessor(fs_bg_, file_values);
    JS_CAN_THROW(file_accessor.CheckValid());

    const Chars* data_ptr = 0;
    Chars data;
    if (args.Length() > 2) {
        const DataBg* data_bg_ptr = DataBg::GetJSClass().Cast(args[2]);
        if (data_bg_ptr) {
            data_ptr = &data_bg_ptr->GetData();
        } else {
            String::Utf8Value utf8_value(args[2]);
            data.assign(*utf8_value, *utf8_value + utf8_value.length());
            data_ptr = &data;
        }
    }
        
    Chars result;
    AppAccessor::Status status = (
        app_accessor_.Process(app_name_,
                              data_ptr,
                              file_accessor.GetFullPathes(),
                              Stringify(args[0]),
                              result));
    switch (status) {
    case AppAccessor::OK:
        KU_ASSERT(result.size() >= 3);
        if (string(&result[0], 3) == "OK\n")
            return String::New(&result[3], result.size() - 3);
        KU_ASSERT(string(&result[0], 6) == "ERROR\n");
        JS_THROW(Error, "Exception occured in \"" + app_name_ + "\" app");
        break;
    case AppAccessor::NO_SUCH_APP:
        JS_THROW(Error, "No such app");
        break;
    case AppAccessor::INVALID_APP_NAME:
        JS_THROW(Error, "Invalid app name");
        break;
    case AppAccessor::SELF_CALL:
        JS_THROW(Error, "Self call is forbidden");
        break;
    default:
        KU_ASSERT(status == AppAccessor::TIMED_OUT);
        JS_THROW(Error, "Timed out");
    }        
    return Handle<v8::Value>();
}

////////////////////////////////////////////////////////////////////////////////
// AppCatalogBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// ak.apps background
    class AppCatalogBg {
    public:
        DECLARE_JS_CLASS(AppCatalogBg);

        AppCatalogBg(AppAccessor& app_accessor, FSBg& fs_bg);

    private:
        AppAccessor& app_accessor_;
        FSBg& fs_bg_;
        
        DECLARE_JS_CALLBACK2(Handle<v8::Value>, GetAppCb,
                             Local<String>,
                             const AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(Handle<Boolean>, HasAppCb,
                             Local<String>,
                             const AccessorInfo&) const;
    };
}


DEFINE_JS_CLASS(AppCatalogBg, "Apps",
                object_template, /*proto_template*/)
{
    AppBg::GetJSClass();
    object_template->SetNamedPropertyHandler(GetAppCb, 0, HasAppCb);
}


AppCatalogBg::AppCatalogBg(AppAccessor& app_accessor, FSBg& fs_bg)
    : app_accessor_(app_accessor)
    , fs_bg_(fs_bg)
{
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, AppCatalogBg, GetAppCb,
                    Local<String>, property,
                    const AccessorInfo&, /*info*/) const
{
    return JSNew<AppBg>(ref(app_accessor_), ref(fs_bg_), Stringify(property));
}


DEFINE_JS_CALLBACK2(Handle<Boolean>, AppCatalogBg, HasAppCb,
                    Local<String>, property,
                    const AccessorInfo&, /*info*/) const
{
    return Boolean::New(app_accessor_.Exists(Stringify(property)));
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

        bool operator()(const string& path, Chars& data) const;

        bool operator()(const string& app_name,
                        const string& path,
                        Chars& data) const;
        
    private:
        string code_path_;
        string include_path_;

        bool Read(const string& base_path,
                  const string& path,
                  Chars& data) const;
    };
}


CodeReader::CodeReader(const string& code_path, const string& include_path)
    : code_path_(code_path), include_path_(include_path)
{
}


bool CodeReader::operator()(const string& path, Chars& data) const
{
    return Read(code_path_, path, data);
}


bool CodeReader::operator()(const string& app_name,
                            const string& path,
                            Chars& data) const
{
    if (app_name.find_first_of('/') != string::npos) {
        JS_THROW(Error, "App name could not contain slashes");
        return false;
    }
    return Read(include_path_ + '/' + app_name, path, data);
}


bool CodeReader::Read(const string& base_path,
                      const string& path,
                      Chars& data) const
{
    if (GetPathDepth(path) <= 0) {
        JS_THROW(Error, "Code path \"" + path + "\" is illegal");
        return false;
    }
    return ReadFileData(base_path + '/' + path, data);
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

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, RunCb,
                             const Arguments&) const;
    };
}


DEFINE_JS_CLASS(ScriptBg, "Script", /*object_template*/, proto_template)
{
    SetFunction(proto_template, "_run", RunCb);
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
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 0);
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

        AKBg(const CodeReader& code_reader);
        
        void InitConstructors(Handle<Object> ak) const;
        
    private:
        const CodeReader& code_reader_;
        
        DECLARE_JS_CALLBACK1(Handle<v8::Value>, PrintCb,
                             const Arguments&) const;
        
        DECLARE_JS_CALLBACK1(Handle<v8::Value>, SetObjectPropCb,
                             const Arguments&) const;
        
        DECLARE_JS_CALLBACK1(Handle<v8::Value>, ReadCodeCb,
                             const Arguments&) const;
        
        DECLARE_JS_CALLBACK1(Handle<v8::Value>, CompileCb,
                             const Arguments&) const;
    };


    template <typename BgT>
    void SetObjectTemplate(Handle<ObjectTemplate> holder_template,
                           const string& name)
    {
        holder_template->Set(name.c_str(),
                             BgT::GetJSClass().GetObjectTemplate());
    }
}


DEFINE_JS_CLASS(AKBg, "AK", object_template, proto_template)
{
    ScriptBg::GetJSClass();
    SetFunction(proto_template, "_print", PrintCb);
    SetFunction(proto_template, "_setObjectProp", SetObjectPropCb);
    SetFunction(proto_template, "_readCode", ReadCodeCb);
    SetFunction(proto_template, "_compile", CompileCb);
    SetObjectTemplate<DBBg>(object_template, "db");
    SetObjectTemplate<RelCatalogBg>(object_template, "rels");
    SetObjectTemplate<TypeCatalogBg>(object_template, "types");
    SetObjectTemplate<ConstrCatalogBg>(object_template, "constrs");
    SetObjectTemplate<FSBg>(object_template, "fs");
    SetObjectTemplate<AppCatalogBg>(object_template, "apps");
}


AKBg::AKBg(const CodeReader& code_reader)
    : code_reader_(code_reader)
{
}


void AKBg::InitConstructors(Handle<Object> ak) const
{
    JSClassBase::InitConstructors(ak);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, AKBg, PrintCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 1);
    cerr << Stringify(args[0]);
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, AKBg, SetObjectPropCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 4);
    JS_TYPE_CHECK(args[0]->IsObject(), "Can't set property of non-object");
    Handle<Object> object(args[0]->ToObject());
    JS_TYPE_CHECK(args[2]->IsInt32(), "Property attribute must be integer");
    int32_t attributes = args[2]->Int32Value();
    JS_CHECK(attributes >= 0 && attributes < 8,
             "Property attribute must be a "
             "non-negative integer less than 8");
    object->Set(args[1], args[3], static_cast<PropertyAttribute>(attributes));
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, AKBg, ReadCodeCb,
                    const Arguments&, args) const
{
    Chars data;
    if (args.Length() == 1)
        JS_CAN_THROW(code_reader_(Stringify(args[0]),
                                  data));
    else if (args.Length() == 2)
        JS_CAN_THROW(code_reader_(Stringify(args[0]),
                                  Stringify(args[1]),
                                  data));
    else
        JS_CHECK(false, "One or two arguments required");
    return String::New(&data[0], data.size());
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, AKBg, CompileCb,
                    const Arguments&, args) const
{
    Handle<Script> script;
    if (args.Length() == 1)
        script = Script::Compile(args[0]->ToString());
    else if (args.Length() == 2)
        script = Script::Compile(args[0]->ToString(), args[1]);
    else
        JS_CHECK(false, "One or two arguments required");
    if (script.IsEmpty())
        return Handle<v8::Value>();
    return JSNew<ScriptBg>(script);
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
    Handle<ObjectTemplate>
        ak_object_template(AKBg::GetJSClass().GetObjectTemplate());
    object_template->Set(String::NewSymbol("ak"),
                         ak_object_template,
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
// Caller
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class Caller : public Transactor {
    public:
        Caller(AccessHolder& access_holder,
               Handle<Function> function,
               Handle<Object> object,
               Handle<v8::Value> arg);
        virtual void operator()(Access& access);
        virtual void Reset();
        auto_ptr<Response> GetResult();

    private:
        AccessHolder& access_holder_;
        Handle<Function> function_;
        Handle<Object> object_;
        Handle<v8::Value> arg_;
        auto_ptr<Response> response_ptr_;
    };
}


Caller::Caller(AccessHolder& access_holder,
               Handle<Function> function,
               Handle<Object> object,
               Handle<v8::Value> arg)
    : access_holder_(access_holder)
    , function_(function)
    , object_(object)
    , arg_(arg)
{
}


void Caller::operator()(Access& access)
{
    AccessHolder::Scope access_scope(access_holder_, access);
    TryCatch try_catch;
    Handle<v8::Value> result(function_->Call(object_, 1, &arg_));
    // I don't know the difference in these conditions but together
    // they handle all cases
    if (try_catch.HasCaught() || result.IsEmpty())
        response_ptr_.reset(new ExceptionResponse(try_catch));
    else
        response_ptr_.reset(new OkResponse(result));
}


void Caller::Reset()
{
    response_ptr_.reset();
}


auto_ptr<Response> Caller::GetResult()
{
    KU_ASSERT(response_ptr_.get());
    return response_ptr_;
}

////////////////////////////////////////////////////////////////////////////////
// Program::Impl
////////////////////////////////////////////////////////////////////////////////

class Program::Impl {
public:
    Impl(const string& app_name,
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
                               const string& requester_app);

    auto_ptr<Response> Eval(const string& user, const Chars& expr);
    bool IsOperable() const;
    
private:
    bool initialized_;
    DB& db_;
    AccessHolder access_holder_;
    CodeReader code_reader_;
    GlobalBg global_bg_;
    AKBg ak_bg_;
    DBBg db_bg_;
    TypeCatalogBg type_catalog_bg_;
    RelCatalogBg rel_catalog_bg_;
    ConstrCatalogBg constr_catalog_bg_;
    FSBg fs_bg_;
    AppCatalogBg app_catalog_bg_;
    Persistent<Context> context_;
    Persistent<Object> ak_;

    void SetInternal(Handle<Object> object,
                     const string& field,
                     void* ptr) const;
    
    auto_ptr<Response> Call(const string& user,
                            const Chars& input,
                            const Strings& file_pathes,
                            auto_ptr<Chars> data_ptr,
                            const string& requester_app,
                            Handle<Object> object,
                            const string& func_name);
};


Program::Impl::Impl(const string& app_name,
                    const string& code_dir,
                    const string& include_dir,
                    const string& media_dir,
                    DB& db,
                    AppAccessor& app_accessor)
    : initialized_(false)
    , db_(db)
    , code_reader_(code_dir, include_dir)
    , global_bg_()
    , ak_bg_(code_reader_)
    , db_bg_(access_holder_)
    , rel_catalog_bg_(access_holder_)
    , fs_bg_(media_dir)
    , app_catalog_bg_(app_accessor, fs_bg_)
{
    ResourceConstraints rc;
    rc.set_max_young_space_size(MAX_YOUNG_SPACE_SIZE);
    rc.set_max_old_space_size(MAX_OLD_SPACE_SIZE);
    bool ret = v8::SetResourceConstraints(&rc);
    KU_ASSERT(ret);
    
    HandleScope handle_scope;
    context_ = Context::New(NULL, GlobalBg::GetJSClass().GetObjectTemplate());
    V8::IgnoreOutOfMemoryException();
    Handle<Object> global_proto(context_->Global()->GetPrototype()->ToObject());
    global_proto->SetInternalField(0, External::New(&global_bg_));
    SetInternal(global_proto, "ak", &ak_bg_);
    ak_ = Persistent<Object>::New(
        global_proto->Get(String::NewSymbol("ak"))->ToObject());
    SetInternal(ak_, "db", &db_bg_);
    SetInternal(ak_, "types", &type_catalog_bg_);
    SetInternal(ak_, "rels", &rel_catalog_bg_);
    SetInternal(ak_, "constrs", &constr_catalog_bg_);
    SetInternal(ak_, "fs", &fs_bg_);
    SetInternal(ak_, "apps", &app_catalog_bg_);

    Context::Scope context_scope(context_);
    ak_bg_.InitConstructors(ak_);
    ak_->Set(String::NewSymbol("_appName"),
             String::New(app_name.c_str()),
             DontEnum);
    // Run include.js script
    Handle<Script> script(Script::Compile(String::New(INCLUDE_JS,
                                                      sizeof(INCLUDE_JS)),
                                          String::New("native include.js")));
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
                                          const string& requester_app)
{
    return Call(user, request,
                file_pathes, data_ptr, requester_app,
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


auto_ptr<Response> Program::Impl::Call(const string& user,
                                       const Chars& input,
                                       const Strings& file_pathes,
                                       auto_ptr<Chars> data_ptr,
                                       const string& requester_app,
                                       Handle<Object> object,
                                       const string& func_name)
{
    HandleScope handle_scope;
    Context::Scope context_scope(context_);
    if (!initialized_) {
        Handle<Function> include_func(
            Handle<Function>::Cast(ak_->Get(String::NewSymbol("include"))));
        KU_ASSERT(!include_func.IsEmpty());
        Caller include_caller(access_holder_,
                              include_func,
                              ak_,
                              String::New("__main__.js"));
        db_.Perform(include_caller);
        auto_ptr<Response> response_ptr(include_caller.GetResult());
        if (response_ptr->GetStatus() != "OK")
            return response_ptr;
        initialized_ = true;
    }
    
    Handle<v8::Value> func_value(object->Get(String::NewSymbol(func_name.c_str())));
    if (func_value.IsEmpty() || !func_value->IsFunction())
        return auto_ptr<Response>(
            new ErrorResponse('"' + func_name + "\" is not a function"));
    
    Handle<v8::Value> data_value;
    if (data_ptr.get())
        data_value = JSNew<DataBg>(data_ptr);
    else
        data_value = Null();
    ak_->Set(String::NewSymbol("_data"), data_value, DontEnum);

    Handle<Array> file_array(Array::New());
    vector<Handle<Object> > files;
    files.reserve(file_pathes.size());
    for (size_t i = 0; i < file_pathes.size(); ++i) {
        Handle<Object> file(JSNew<TmpFileBg>(file_pathes[i]));
        files.push_back(file);
        file_array->Set(Integer::New(i), file);
    }
    ak_->Set(String::NewSymbol("_files"), file_array, DontEnum);

    ak_->Set(String::NewSymbol("_user"), String::New(user.c_str()), DontEnum);
    ak_->Set(String::NewSymbol("_requesterAppName"),
             String::New(requester_app.c_str()),
             DontEnum);

    Caller caller(access_holder_,
                  Handle<Function>::Cast(func_value),
                  object,
                  String::New(&input[0], input.size()));
    db_.Perform(caller);
    auto_ptr<Response> result(caller.GetResult());
    
    BOOST_FOREACH(Handle<Object> file, files)
        TmpFileBg::GetJSClass().Cast(file)->ClearPath();
    
    return result;    
}
                                       

void Program::Impl::SetInternal(Handle<Object> object,
                                const string& field,
                                void* ptr) const
{
    object->Get(String::NewSymbol(field.c_str()))->
        ToObject()->SetInternalField(0, External::New(ptr));
}

////////////////////////////////////////////////////////////////////////////////
// Program
////////////////////////////////////////////////////////////////////////////////

Program::Program(const string& app_name,
                 const string& code_dir,
                 const string& include_dir,
                 const string& media_dir,
                 DB& db,
                 AppAccessor& app_accessor)
    : pimpl_(new Impl(app_name,
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
                                    const string& requester_app)
{
    return pimpl_->Process(user, request, file_pathes, data_ptr, requester_app);
}


auto_ptr<Response> Program::Eval(const string& user, const Chars& expr)
{
    return pimpl_->Eval(user, expr);
}


bool Program::IsOperable() const
{
    return pimpl_->IsOperable();
}
