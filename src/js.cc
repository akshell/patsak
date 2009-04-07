
// (c) 2009 by Anton Korenyushkin

/// \file js.h
/// JavaScript interpreter impl

#include "js.h"
#include "js-common.h"
#include "js-db.h"
#include "db.h"

#include <boost/foreach.hpp>
#include <boost/utility.hpp>

#include <stdlib.h>
#include <libgen.h>
#include <vector>
#include <fstream>
#include <sstream>
#include <memory>


using namespace ku;
using namespace std;
using namespace v8;
using boost::noncopyable;


////////////////////////////////////////////////////////////////////////////////
// Printer
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Output stream print functor
    class Printer {
    public:
        Printer(ostream& out) : out_(out) {}
        void operator()(const string& str) const { out_ << str; }

    private:
        ostream& out_;
    };
}

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
        Handle<String> ReadFile() const;

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
    if (args.Length() != 1) {
        JS_THROW(Error, "import() must be called with one argument");
        return Handle<v8::Value>();
    }
    string path(Canonicalize(DirName(file_path_) + '/' + Stringify(args[0])));
    if (path.empty() ||
        path.substr(0, base_dir_.size()) != base_dir_) {
        JS_THROW(Error, "Bad import() path");
        return Handle<v8::Value>();
    }        
    CurrFileScope curr_file_scope(*this, path);
    Handle<String> expr(ReadFile());
    if (expr.IsEmpty())
        return Handle<v8::Value>();
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
    if (Eval(ReadFile()).IsEmpty())
        return false;
    ready_ = true;
    return true;
}


Handle<v8::Value> Importer::Eval(Handle<String> expr) const
{
    if (expr.IsEmpty())
        return Handle<v8::Value>();
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


Handle<String> Importer::ReadFile() const
{
    ifstream file(file_path_.c_str(), ios::in | ios::binary | ios::ate);
    if (!file.is_open()) {
        JS_THROW(Error, "Can't open file " + file_path_);
        return Handle<String>();
    }
    ifstream::pos_type size = file.tellg();
    file.seekg(0, ios::beg);
    vector<char> buf(size);
    file.read(&buf[0], size);
    file.close();
    return String::New(&buf[0], size);
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
// KuBg and GlobalBg declarations
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// ku background
    class KuBg {
    public:
        DECLARE_JS_CLASS(KuBg);

        KuBg(const Printer& printer);
        
        void InitConstructors(Handle<Object> ku) const;
        
    private:
        Printer printer_;

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
// KuBg definitions
////////////////////////////////////////////////////////////////////////////////

DEFINE_JS_CLASS(KuBg, "Ku", object_template, proto_template)
{
    proto_template->Set("print", FunctionTemplate::New(PrintCb));    
    proto_template->Set("setObjectProp",
                         FunctionTemplate::New(SetObjectPropCb));
    object_template->Set("db",
                         DbBg::GetJSClass().GetObjectTemplate());
    object_template->Set("rel",
                         RelCatalogBg::GetJSClass().GetObjectTemplate());
    object_template->Set("type",
                         TypeCatalogBg::GetJSClass().GetObjectTemplate());
    object_template->Set("constr",
                         ConstrCatalogBg::GetJSClass().GetObjectTemplate());
}


KuBg::KuBg(const Printer& printer)
    : printer_(printer)
{
}


void KuBg::InitConstructors(Handle<Object> ku) const
{
    ku->Set(String::NewSymbol("Global"),
            GlobalBg::GetJSClass().GetFunction());
    ku->Set(String::NewSymbol("Ku"),
            KuBg::GetJSClass().GetFunction());
    InitDBConstructors(ku);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, KuBg, PrintCb,
                    const Arguments&, args) const
{
    for (int i = 0; i < args.Length(); ++i)
        printer_(Stringify(args[i]));
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, KuBg, SetObjectPropCb,
                    const Arguments&, args) const
{
    if (args.Length() != 4) {
        JS_THROW(Error, "setObjectProp() requires 4 arguments");
        return Handle<v8::Value>();
    }
    if (!args[0]->IsObject()) {
        JS_THROW(Error, "First setObjectProp() argument must be object");
        return Handle<v8::Value>();
    }
    Handle<Object> object(args[0]->ToObject());
    if (!args[2]->IsInt32()) {
        JS_THROW(Error, "Third setObjectProp() argument must be integer");
        return Handle<v8::Value>();
    }
    int32_t attributes = args[2]->Int32Value();
    if (attributes < 0 || attributes > 7) {
        JS_THROW(Error,
                 "Property attribute must be a "
                 "non-negative integer less than 8");
        return Handle<v8::Value>();
    }
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
        ku_object_template(KuBg::GetJSClass().GetObjectTemplate());
    object_template->Set("ku", ku_object_template);
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
            << "\nCOLUMN " << message->GetStartColumn();
    } else if (!exception.IsEmpty())
        oss << "EXCEPTION " << Stringify(exception);
    else
        oss << "UNKNOWN_EXCEPTION";
    oss << '\n';
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
                  const string& expr);
        virtual void operator()(Access& access);
        virtual void Reset();
        auto_ptr<EvalResult> GetResult();

    private:
        AccessHolder& access_holder_;
        Importer& importer_;
        string expr_;
        auto_ptr<EvalResult> result_ptr_;

        void Fail(const TryCatch& try_catch);
    };
}


Evaluator::Evaluator(AccessHolder& access_holder,
                     Importer& importer,
                     const string& expr)
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
    Handle<Script> script = Script::Compile(String::New(expr_.c_str()));
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
    Impl(const string& file_path, DB& db, ostream& out);
    ~Impl();
    auto_ptr<EvalResult> Eval(const string& expr);
    
private:
    DB& db_;
    AccessHolder access_holder_;
    Importer importer_;
    GlobalBg global_bg_;
    KuBg ku_bg_;
    DbBg db_bg_;
    TypeCatalogBg type_catalog_bg_;
    RelCatalogBg rel_catalog_bg_;
    ConstrCatalogBg constr_catalog_bg_;
    Persistent<Context> context_;

    void SetInternal(Handle<Object> object,
                     const string& field,
                     void* ptr) const;
};


Program::Impl::Impl(const string& file_path, DB& db, ostream& out)
    : db_(db)
    , importer_(file_path)
    , global_bg_(importer_)
    , ku_bg_(Printer(out))
    , db_bg_(access_holder_)
    , rel_catalog_bg_(access_holder_)
{
    HandleScope handle_scope;
    context_ = Context::New(NULL, GlobalBg::GetJSClass().GetObjectTemplate());
    context_->DetachGlobal();
    Handle<Object> global(context_->Global());
    global->SetInternalField(0, External::New(&global_bg_));
    SetInternal(global, "ku", &ku_bg_);
    Handle<Object> ku(global->Get(String::NewSymbol("ku"))->ToObject());
    SetInternal(ku, "db", &db_bg_);
    SetInternal(ku, "type", &type_catalog_bg_);
    SetInternal(ku, "rel", &rel_catalog_bg_);
    SetInternal(ku, "constr", &constr_catalog_bg_);

    Context::Scope context_scope(context_);
    ku_bg_.InitConstructors(ku);
}


Program::Impl::~Impl()
{
    context_.Dispose();
}


auto_ptr<EvalResult> Program::Impl::Eval(const string& expr_str)
{
    HandleScope handle_scope;
    Context::Scope context_scope(context_);    
    Evaluator evaluator(access_holder_, importer_, expr_str);
    db_.Perform(evaluator);
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

Program::Program(const string& file_path, DB& db, ostream& out)
    : pimpl_(new Impl(file_path, db, out))
{
}


Program::~Program()
{
}


auto_ptr<EvalResult> Program::Eval(const string& expr_str)
{
    return pimpl_->Eval(expr_str);
}
