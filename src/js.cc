
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


////////////////////////////////////////////////////////////////////////////////
// Importer
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// import() functor
    class Importer : noncopyable {
    public:
        Importer(const string& file_path);
        Handle<v8::Value> operator()(const Arguments& args);
        bool GetReady();

    private:
        class CurrFileScope {
        public:
            CurrFileScope(Importer& importer, const string& new_file_path);
            ~CurrFileScope();

        private:
            Importer& importer_;
            string old_file_path_;
        };
        
        string file_path_;
        const string base_dir_;
        bool ready_;

        Handle<v8::Value> Eval(Handle<String> expr) const;
        bool ReadFile(Handle<String>& str) const;

        void ThrowImportError(Handle<v8::Value> exception,
                              Handle<Message> message) const;
            
        static string DirName(const string& path);
        static string Canonicalize(const string& path);
    };
}


Importer::CurrFileScope::CurrFileScope(Importer& importer,
                                       const string& new_file_path)
    : importer_(importer)
    , old_file_path_(importer.file_path_)
{
    importer_.file_path_ = new_file_path;
}


Importer::CurrFileScope::~CurrFileScope()
{
    importer_.file_path_ = old_file_path_;
}


Importer::Importer(const string& file_path)
    : file_path_(Canonicalize(file_path))
    , base_dir_(DirName(file_path_))
    , ready_(false)
{
    if (file_path_.empty())
        Fail("Bad initial path " + file_path);
}


Handle<v8::Value> Importer::operator()(const Arguments& args)
{
    JS_CHECK_LENGTH(args, 1);
    string path(Canonicalize(DirName(file_path_) + '/' + Stringify(args[0])));
    JS_CHECK(!path.empty() && path.substr(0, base_dir_.size()) == base_dir_,
             "Bad import() path");
    CurrFileScope curr_file_scope(*this, path);
    Handle<String> expr;
    JS_CAN_THROW(ReadFile(expr));
    Handle<v8::Value> result;
    Handle<v8::Value> exception;
    Handle<Message> message;
    {
        TryCatch try_catch;
        result = Eval(expr);
        exception = try_catch.Exception();
        message = try_catch.Message();
    }
    if (result.IsEmpty()) {
        ThrowImportError(exception, message);
        return Handle<v8::Value>();
    }
    return result;
}


bool Importer::GetReady()
{
    if (ready_)
        return true;
    Handle<String> expr;
    if (!ReadFile(expr))
        return false;
    if (Eval(expr).IsEmpty())
        return false;
    ready_ = true;
    return true;
}


Handle<v8::Value> Importer::Eval(Handle<String> expr) const
{
    Handle<String>
        resouce_name(String::New(file_path_.substr(base_dir_.size()).c_str()));
    Handle<Script> script = Script::Compile(expr, resouce_name);
    if (script.IsEmpty())
        return Handle<v8::Value>();
    Handle<v8::Value> result = script->Run();
    if (result.IsEmpty())
        return Handle<v8::Value>();
    return result;
}


bool Importer::ReadFile(Handle<String>& str) const
{
    Chars data;
    if (!ReadFileData(file_path_, data))
        return false;
    str = String::New(&data[0], data.size());
    return true;
}


void Importer::ThrowImportError(Handle<v8::Value> exception,
                                Handle<Message> message) const
{
    ostringstream oss;
    oss << "ImportError in file " << file_path_;
    if (!message.IsEmpty())
        oss << " line " << message->GetLineNumber()
            << " column " << message->GetStartColumn()
            << ": " << Stringify(message->Get());
    else if (!exception.IsEmpty())
        oss << ": " << Stringify(exception);
    JS_THROW(Error, oss.str());
}


string Importer::DirName(const string& path)
{
    vector<char> path_copy(path.c_str(), path.c_str() + path.size() + 1);
    return dirname(&path_copy[0]);
}


string Importer::Canonicalize(const string& path)
{
    char* c_str = canonicalize_file_name(path.c_str());
    if (!c_str)
        return "";
    string result(c_str);
    free(c_str);
    return result;
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
        
        GlobalBg(Importer& importer);
        
    private:
        Importer& importer_;

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

DEFINE_JS_CLASS(GlobalBg, "Global", object_template, /*proto_template*/)
{
    object_template->Set("import", FunctionTemplate::New(ImportCb));

    Handle<ObjectTemplate>
        ak_object_template(AKBg::GetJSClass().GetObjectTemplate());
    object_template->Set(String::NewSymbol("ak"),
                         ak_object_template,
                         ReadOnly | DontDelete);
}


GlobalBg::GlobalBg(Importer& importer)
    : importer_(importer)
{
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, GlobalBg, ImportCb,
                    const Arguments&, args) const
{
    return importer_(args);
}

////////////////////////////////////////////////////////////////////////////////
// OkResult
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Result of a successful evaluation
    class OkResult : public EvalResult {
    public:
        OkResult(Handle<v8::Value> value);
        virtual string GetStatus() const;
        virtual size_t GetSize() const;
        virtual const char* GetData() const;

    private:
        // mutable is due to the fact that String::Utf8Value::length()
        // isn't const for unknown reasons
        mutable String::Utf8Value utf8_value_;
    };  
}


OkResult::OkResult(Handle<v8::Value> value)
    : utf8_value_(value)
{
}


string OkResult::GetStatus() const
{
    return "OK";
}


size_t OkResult::GetSize() const
{
    return static_cast<size_t>(utf8_value_.length());
}


const char* OkResult::GetData() const
{
    return *utf8_value_;
}

////////////////////////////////////////////////////////////////////////////////
// ErrorResult
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Evaluation failure descriptor
    class ErrorResult : public EvalResult {
    public:
        ErrorResult(Handle<v8::Value> exception, Handle<Message> message);

        virtual string GetStatus() const;
        virtual size_t GetSize() const;
        virtual const char* GetData() const;

    private:
        string data_;
    };
}


ErrorResult::ErrorResult(Handle<v8::Value> exception,
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
    data_ = oss.str();
}


string ErrorResult::GetStatus() const
{
    return "ERROR";
}


size_t ErrorResult::GetSize() const
{
    return data_.size();
}


const char* ErrorResult::GetData() const
{
    return data_.data();
}

////////////////////////////////////////////////////////////////////////////////
// Evaluator
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Eval functor
    class Evaluator : public Transactor {
    public:
        Evaluator(AccessHolder& access_holder,
                  Importer& importer,
                  const Chars& expr);
        virtual void operator()(Access& access);
        virtual void Reset();
        auto_ptr<EvalResult> GetResult();

    private:
        AccessHolder& access_holder_;
        Importer& importer_;
        Chars expr_;
        auto_ptr<EvalResult> result_ptr_;

        void Fail(const TryCatch& try_catch);
    };
}


Evaluator::Evaluator(AccessHolder& access_holder,
                     Importer& importer,
                     const Chars& expr)
    : access_holder_(access_holder)
    , importer_(importer)
    , expr_(expr)
{
}


void Evaluator::operator()(Access& access)
{
    AccessHolder::Scope access_scope(access_holder_, access);
    TryCatch try_catch;
    if (!importer_.GetReady()) {
        Fail(try_catch);
        return;
    }
    Handle<Script> script = Script::Compile(String::New(&expr_.front(),
                                                        expr_.size()));
    if (script.IsEmpty()) {
        Fail(try_catch);
        return;
    }
    Handle<v8::Value> result = script->Run();
    if (result.IsEmpty()) {
        Fail(try_catch);
        return;
    }
    result_ptr_.reset(new OkResult(result));
}


void Evaluator::Reset()
{
    result_ptr_.reset();
}


auto_ptr<EvalResult> Evaluator::GetResult()
{
    KU_ASSERT(result_ptr_.get());
    return result_ptr_;
}


void Evaluator::Fail(const TryCatch& try_catch)
{
    KU_ASSERT(!result_ptr_.get());
    result_ptr_.reset(new ErrorResult(try_catch.Exception(),
                                        try_catch.Message()));
}

////////////////////////////////////////////////////////////////////////////////
// Program::Impl
////////////////////////////////////////////////////////////////////////////////

class Program::Impl {
public:
    Impl(const string& file_path, const string& media_path, DB& db);
    ~Impl();
    auto_ptr<EvalResult> Eval(const Chars& expr,
                              const Strings& pathes,
                              auto_ptr<Chars> data_ptr);
    
private:
    DB& db_;
    AccessHolder access_holder_;
    Importer importer_;
    GlobalBg global_bg_;
    AKBg ak_bg_;
    DBBg db_bg_;
    TypeCatalogBg type_catalog_bg_;
    RelCatalogBg rel_catalog_bg_;
    ConstrCatalogBg constr_catalog_bg_;
    FSBg fs_bg_;
    Persistent<Context> context_;
    Persistent<Object> ak_;

    void SetInternal(Handle<Object> object,
                     const string& field,
                     void* ptr) const;
};


Program::Impl::Impl(const string& file_path, const string& media_path, DB& db)
    : db_(db)
    , importer_(file_path)
    , global_bg_(importer_)
    , ak_bg_()
    , db_bg_(access_holder_)
    , rel_catalog_bg_(access_holder_)
    , fs_bg_(media_path)
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

    Context::Scope context_scope(context_);
    ak_bg_.InitConstructors(ak_);
}


Program::Impl::~Impl()
{
    ak_.Dispose();
    context_.Dispose();
}


auto_ptr<EvalResult> Program::Impl::Eval(const Chars& expr,
                                         const Strings& pathes,
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
    files.reserve(pathes.size());
    for (size_t i = 0; i < pathes.size(); ++i) {
        Handle<Object> file(JSNew<TmpFileBg>(pathes[i]));
        files.push_back(file);
        file_array->Set(Integer::New(i), file);
    }
    ak_->Set(String::NewSymbol("_files"), file_array, DontEnum);
         
    Evaluator evaluator(access_holder_, importer_, expr);
    db_.Perform(evaluator);

    BOOST_FOREACH(Handle<Object> file, files)
        TmpFileBg::GetJSClass().Cast(file)->ClearPath();
    
    return evaluator.GetResult();
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

Program::Program(const string& file_path, const string& media_path, DB& db)
    : pimpl_(new Impl(file_path, media_path, db))
{
}


Program::~Program()
{
}


auto_ptr<EvalResult> Program::Eval(const Chars& expr,
                                   const Strings& pathes,
                                   auto_ptr<Chars> data_ptr)
{
    return pimpl_->Eval(expr, pathes, data_ptr);
}
