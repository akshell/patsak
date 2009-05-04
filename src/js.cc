
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
// CodeLoader
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// import() and include() manager
    class CodeLoader {
    public:
        CodeLoader(const string& include_dir, const string& base_path);
        // never throw
        Handle<v8::Value> Import(const string& lib_path,
                                 const string& file_path);
        // never throw
        Handle<v8::Value> Include(const string& path);
        
    private:
        const string include_dir_;
        string base_path_;
        string curr_path_;

        static string DirName(const string& path);
        static string Canonicalize(const string& path);
    };
}


CodeLoader::CodeLoader(const string& include_dir, const string& base_path)
    : include_dir_(Canonicalize(include_dir))
    , base_path_(Canonicalize(base_path))
    , curr_path_(base_path_)
{
    KU_ASSERT(!include_dir.empty());
    KU_ASSERT(!base_path.empty());
}


Handle<v8::Value> CodeLoader::Import(const string& lib_path,
                                     const string& file_path)
{
    JS_CHECK(GetPathDepth(lib_path) > 0, "Illegal lib path");
    string lib_full_path = Canonicalize(include_dir_ + '/' + lib_path);
    JS_CHECK(!lib_full_path.empty(), "Lib " + lib_path + " does not exist");
    string old_base_path(base_path_);
    string old_curr_path(curr_path_);
    curr_path_ = base_path_ = lib_full_path;
    Handle<v8::Value> result(Include(file_path));
    base_path_ = old_base_path;
    curr_path_ = old_curr_path;
    return result;
}


Handle<v8::Value> CodeLoader::Include(const string& path)
{
    JS_CHECK(!path.empty(), "include path is empty");
    string full_path(Canonicalize(path[0] == '/'
                                  ? base_path_ + path
                                  : curr_path_ + '/' + path));
    // This conditions are checked together in order to prevent the
    // possibility of scanning file system by such includes
    JS_CHECK(!full_path.empty() &&
             full_path.substr(0, base_path_.size()) == base_path_,
             "Bad include() path");
    Chars data;
    JS_CAN_THROW(ReadFileData(full_path, data));
    Handle<String> expr(String::New(&data[0], data.size()));
    Handle<v8::Value> result;
    Handle<v8::Value> exception;
    Handle<Message> message;
    {
        TryCatch try_catch;
        Handle<String> resource_name(String::New(path.c_str()));
        Handle<Script> script = Script::Compile(expr, resource_name);
        if (!script.IsEmpty()) {
            string old_curr_path(curr_path_);
            curr_path_ = DirName(full_path);
            result = script->Run();
            curr_path_ = old_curr_path;
        }
        exception = try_catch.Exception();
        message = try_catch.Message();
    }
    if (result.IsEmpty()) {
        ostringstream oss;
        oss << "ImportError in file " << path;
        if (!message.IsEmpty())
            oss << " line " << message->GetLineNumber()
                << " column " << message->GetStartColumn()
                << ": " << Stringify(message->Get());
        else if (!exception.IsEmpty())
            oss << ": " << Stringify(exception);
        JS_THROW(Error, oss.str());
    }
    return result;    
}


string CodeLoader::DirName(const string& path)
{
    vector<char> path_copy(path.c_str(), path.c_str() + path.size() + 1);
    return dirname(&path_copy[0]);
}


string CodeLoader::Canonicalize(const string& path)
{
    char* c_str = canonicalize_file_name(path.c_str());
    if (!c_str)
        return "";
    string result(c_str);
    free(c_str);
    return result;
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
              const string& root_path,
              const string& app_name);

    private:
        AppAccessor& app_accessor_;
        string root_path_;
        string app_name_;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, CallCb,
                             const Arguments&) const;

        bool GetFullPath(const string& rel_path, string& full_path) const;
    };
}


DEFINE_JS_CLASS(AppBg, "App", /*object_template*/, proto_template)
{
    proto_template->Set("call", FunctionTemplate::New(CallCb));
}


AppBg::AppBg(AppAccessor& app_accessor,
             const string& root_path,
             const string& app_name)
    : app_accessor_(app_accessor)
    , root_path_(root_path)
    , app_name_(app_name)
{
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, AppBg, CallCb,
                    const Arguments&, args) const
{
    JS_CHECK(args.Length() > 0 && args.Length() < 4,
             "call() takes one, two or three arguments");
    Strings file_pathes;
    const Chars* data_ptr = 0;

    if (args.Length() > 1) {
        int32_t length = GetArrayLikeLength(args[1]);
        JS_TYPE_CHECK(length != -1,
                      "Second call() argument must be array-like");
        file_pathes.reserve(length);
        for (int32_t i = 0; i < length; ++i) {
            Handle<v8::Value> item(GetArrayLikeItem(args[1], i));
            if (item.IsEmpty())
                return Handle<v8::Value>();
            string full_path;
            JS_CAN_THROW(GetFullPath(Stringify(item), full_path));
            file_pathes.push_back(full_path);
        }
    }

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
    AppAccessor::Status status = app_accessor_.Process(app_name_,
                                                       data_ptr,
                                                       file_pathes,
                                                       Stringify(args[0]),
                                                       result);
    switch (status) {
    case AppAccessor::OK:
        KU_ASSERT(result.size() >= 3);
        if (string(&result[0], 3) == "OK\n")
            return String::New(&result[3], result.size() - 3);
        KU_ASSERT(string(&result[0], 6) == "ERROR\n");
        JS_THROW(Error, "Exception occured in " + app_name_ + " app");
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


bool AppBg::GetFullPath(const string& rel_path, string& full_path) const
{
    int depth = GetPathDepth(rel_path);
    if (depth <= 0) {
        JS_THROW(Error, "Bad file path");
        return false;
    }
    full_path = root_path_ + '/' + rel_path;
    struct stat st;
    if (stat(full_path.c_str(), &st) == -1) {
        KU_ASSERT(errno == ENOENT);
        JS_THROW(Error, "No such file: " + rel_path);
        return false;
    }
    if (S_ISDIR(st.st_mode)) {
        JS_THROW(Error, "Can't pass directories");
        return false;
    }
    KU_ASSERT(S_ISREG(st.st_mode));
    return true;
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

        AppCatalogBg(AppAccessor& app_accessor, const string& root_path);

    private:
        AppAccessor& app_accessor_;
        string root_path_;
        
        DECLARE_JS_CALLBACK2(Handle<v8::Value>, GetAppCb,
                             Local<String>,
                             const AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(Handle<Boolean>, HasAppCb,
                             Local<String>,
                             const AccessorInfo&) const;
    };
}


DEFINE_JS_CLASS(AppCatalogBg, "AppCatalogg",
                object_template, /*proto_template*/)
{
    object_template->SetNamedPropertyHandler(GetAppCb, 0, HasAppCb);
}


AppCatalogBg::AppCatalogBg(AppAccessor& app_accessor, const string& root_path)
    : app_accessor_(app_accessor)
    , root_path_(root_path)
{
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, AppCatalogBg, GetAppCb,
                    Local<String>, property,
                    const AccessorInfo&, /*info*/) const
{
    return JSNew<AppBg>(ref(app_accessor_), root_path_, Stringify(property));
}


DEFINE_JS_CALLBACK2(Handle<Boolean>, AppCatalogBg, HasAppCb,
                    Local<String>, property,
                    const AccessorInfo&, /*info*/) const
{
    return Boolean::New(app_accessor_.Exists(Stringify(property)));
}

////////////////////////////////////////////////////////////////////////////////
// AKBg and GlobalBg declarations
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// ku background
    class AKBg {
    public:
        DECLARE_JS_CLASS(AKBg);

        AKBg();
        
        void InitConstructors(Handle<Object> ak) const;
        
    private:
        DECLARE_JS_CALLBACK1(Handle<v8::Value>, PrintCb,
                             const Arguments&) const;
        
        DECLARE_JS_CALLBACK1(Handle<v8::Value>, SetObjectPropCb,
                             const Arguments&) const;
    };


    /// Global background
    class GlobalBg {
    public:
        DECLARE_JS_CLASS(GlobalBg);
        
        GlobalBg(CodeLoader& code_loader);
        
    private:
        CodeLoader& code_loader_;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, IncludeCb,
                             const Arguments&) const;
        
        DECLARE_JS_CALLBACK1(Handle<v8::Value>, ImportCb,
                             const Arguments&) const;
    };    
}

////////////////////////////////////////////////////////////////////////////////
// AKBg definitions
////////////////////////////////////////////////////////////////////////////////

DEFINE_JS_CLASS(AKBg, "AK", object_template, proto_template)
{
    proto_template->Set(String::NewSymbol("_print"),
                        FunctionTemplate::New(PrintCb),
                        ReadOnly | DontEnum | DontDelete);
    proto_template->Set("setObjectProp",
                         FunctionTemplate::New(SetObjectPropCb));
    object_template->Set("db",
                         DBBg::GetJSClass().GetObjectTemplate());
    object_template->Set("rel",
                         RelCatalogBg::GetJSClass().GetObjectTemplate());
    object_template->Set("type",
                         TypeCatalogBg::GetJSClass().GetObjectTemplate());
    object_template->Set("constr",
                         ConstrCatalogBg::GetJSClass().GetObjectTemplate());
    object_template->Set("fs",
                         FSBg::GetJSClass().GetObjectTemplate());
    object_template->Set("apps",
                         AppCatalogBg::GetJSClass().GetObjectTemplate());
}


AKBg::AKBg()
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
    JS_TYPE_CHECK(args[0]->IsObject(),
                  "First setObjectProp() argument must be object");
    Handle<Object> object(args[0]->ToObject());
    JS_TYPE_CHECK(args[2]->IsInt32(),
                  "Third setObjectProp() argument must be integer");
    int32_t attributes = args[2]->Int32Value();
    JS_CHECK(attributes >= 0 && attributes < 8,
             "Property attribute must be a "
             "non-negative integer less than 8");
    object->Set(args[1], args[3], static_cast<PropertyAttribute>(attributes));
    return Undefined();
}

////////////////////////////////////////////////////////////////////////////////
// GlobalBg definitions
////////////////////////////////////////////////////////////////////////////////

DEFINE_JS_CLASS(GlobalBg, "Global", object_template, proto_template)
{
    proto_template->Set("import", FunctionTemplate::New(ImportCb));
    proto_template->Set("include", FunctionTemplate::New(IncludeCb));

    Handle<ObjectTemplate>
        ak_object_template(AKBg::GetJSClass().GetObjectTemplate());
    object_template->Set(String::NewSymbol("ak"),
                         ak_object_template,
                         ReadOnly | DontDelete);
}


GlobalBg::GlobalBg(CodeLoader& code_loader)
    : code_loader_(code_loader)
{
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, GlobalBg, ImportCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 2);
    return code_loader_.Import(Stringify(args[0]), Stringify(args[1]));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, GlobalBg, IncludeCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 1);
    return code_loader_.Include(Stringify(args[0]));
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
        ExceptionResponse(Handle<v8::Value> exception, Handle<Message> message);
        
    private:
        static string MakeExceptionDescr(Handle<v8::Value> exception,
                                         Handle<Message> message);
    };
}


ExceptionResponse::ExceptionResponse(Handle<v8::Value> exception,
                                     Handle<Message> message)
    : ErrorResponse(MakeExceptionDescr(exception, message))
{
}


string ExceptionResponse::MakeExceptionDescr(Handle<v8::Value> exception,
                                             Handle<Message> message)
{
    ostringstream oss;
    if (!message.IsEmpty()) {
        oss << "EXCEPTION " << Stringify(message->Get());
        Handle<v8::Value> resource_name(message->GetScriptResourceName());
        if (!resource_name->IsUndefined())
            oss << "\nFILE " << Stringify(message->GetScriptResourceName());
        oss << "\nLINE " << message->GetLineNumber()
            << "\nCOLUMN " << message->GetStartColumn() << '\n';
    } else if (!exception.IsEmpty()) {
        oss << "EXCEPTION " << Stringify(exception) << '\n';
    }
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
    if (result.IsEmpty())
        response_ptr_.reset(new ExceptionResponse(try_catch.Exception(),
                                                  try_catch.Message()));
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
    Impl(const string& file_dir,
         const string& include_dir,
         const string& media_dir,
         DB& db,
         AppAccessor& app_accessor);
    
    ~Impl();
    
    auto_ptr<Response> Process(const Chars& request,
                               const Strings& pathes,
                               auto_ptr<Chars> data_ptr);
    
private:
    bool initialized_;
    DB& db_;
    AccessHolder access_holder_;
    CodeLoader code_loader_;
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
};


Program::Impl::Impl(const string& code_dir,
                    const string& include_dir,
                    const string& media_dir,
                    DB& db,
                    AppAccessor& app_accessor)
    : initialized_(false)
    , db_(db)
    , code_loader_(include_dir, code_dir)
    , global_bg_(code_loader_)
    , ak_bg_()
    , db_bg_(access_holder_)
    , rel_catalog_bg_(access_holder_)
    , fs_bg_(media_dir)
    , app_catalog_bg_(app_accessor, media_dir)
{
    HandleScope handle_scope;
    context_ = Context::New(NULL, GlobalBg::GetJSClass().GetObjectTemplate());
    context_->DetachGlobal();
    Handle<Object> global(context_->Global());
    global->SetInternalField(0, External::New(&global_bg_));
    SetInternal(global, "ak", &ak_bg_);
    ak_ = Persistent<Object>::New(
        global->Get(String::NewSymbol("ak"))->ToObject());
    SetInternal(ak_, "db", &db_bg_);
    SetInternal(ak_, "type", &type_catalog_bg_);
    SetInternal(ak_, "rel", &rel_catalog_bg_);
    SetInternal(ak_, "constr", &constr_catalog_bg_);
    SetInternal(ak_, "fs", &fs_bg_);
    SetInternal(ak_, "apps", &app_catalog_bg_);

    Context::Scope context_scope(context_);
    ak_bg_.InitConstructors(ak_);
}


Program::Impl::~Impl()
{
    ak_.Dispose();
    context_.Dispose();
}


auto_ptr<Response> Program::Impl::Process(const Chars& request,
                                          const Strings& file_pathes,
                                          auto_ptr<Chars> data_ptr)
{
    HandleScope handle_scope;
    Context::Scope context_scope(context_);
    
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

    auto_ptr<Response> result;
    if (!initialized_) {
        Handle<Function> include_func(
            Handle<Function>::Cast(
                context_->Global()->Get(String::NewSymbol("include"))));
        KU_ASSERT(!include_func.IsEmpty());
        Caller include_caller(access_holder_,
                              include_func,
                              context_->Global(),
                              String::New("main.js"));
        db_.Perform(include_caller);
        result = include_caller.GetResult();
        KU_ASSERT(result.get());
        if (result->GetStatus() == "OK")
            initialized_ = true;
    }
    if (initialized_) {
        Handle<v8::Value> main_value(ak_->Get(String::NewSymbol("_main")));
        if (main_value.IsEmpty() || !main_value->IsFunction()) {
            result = auto_ptr<Response>(
                new ErrorResponse("ak._main is not a function"));
        } else {
            Handle<Function> main_func(Handle<Function>::Cast(main_value));
            Handle<String> arg(String::New(&request[0], request.size()));
            Caller main_caller(access_holder_, main_func, ak_, arg);
            db_.Perform(main_caller);
            result = main_caller.GetResult();
        }
    }
    
    BOOST_FOREACH(Handle<Object> file, files)
        TmpFileBg::GetJSClass().Cast(file)->ClearPath();
    
    KU_ASSERT(result.get());
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

Program::Program(const string& code_dir,
                 const string& include_dir,
                 const string& media_dir,
                 DB& db,
                 AppAccessor& app_accessor)
    : pimpl_(new Impl(code_dir, include_dir, media_dir, db, app_accessor))
{
}


Program::~Program()
{
}


auto_ptr<Response> Program::Process(const Chars& request,
                                   const Strings& file_pathes,
                                   auto_ptr<Chars> data_ptr)
{
    return pimpl_->Process(request, file_pathes, data_ptr);
}
