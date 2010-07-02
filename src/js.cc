
// (c) 2009-2010 by Anton Korenyushkin

#include "js.h"
#include "js-common.h"
#include "js-db.h"
#include "js-fs.h"
#include "js-binary.h"
#include "js-proxy.h"
#include "js-script.h"
#include "js-socket.h"
#include "js-http.h"
#include "db.h"

#include <boost/foreach.hpp>

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
// CoreBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class CoreBg {
    public:
        DECLARE_JS_CLASS(CoreBg);

        CoreBg(const Place& place, const CodeReader& code_reader);
        void Init(Handle<Object> object) const;

    private:
        Place place_;
        const CodeReader& code_reader_;

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
    };
}


DEFINE_JS_CLASS(CoreBg, "Core", object_template, /*proto_template*/)
{
    SetFunction(object_template, "print", PrintCb);
    SetFunction(object_template, "set", SetCb);
    SetFunction(object_template, "readCode", ReadCodeCb);
    SetFunction(object_template, "getCodeModDate", GetCodeModDateCb);
    SetFunction(object_template, "hash", HashCb);
}


CoreBg::CoreBg(const Place& place, const CodeReader& code_reader)
    : place_(place)
    , code_reader_(code_reader)
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
         const string& media_path,
         DB& db);

    ~Impl();

    void Process(int sock_fd);
    void Eval(const Chars& expr, int out_fd);
    bool IsDead() const;

private:
    bool initialized_;
    DB& db_;
    CodeReader code_reader_;
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
                    const string& media_path,
                    DB& db)
    : initialized_(false)
    , db_(db)
    , code_reader_(app_code_path, release_code_path)
    , core_bg_(place, code_reader_)
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

    Context::Scope context_scope(context_);
    core_bg_.Init(core_);
    Set(core_, "db", InitDB());
    Set(core_, "fs", InitFS(media_path));
    Set(core_, "binary", InitBinary());
    Set(core_, "proxy", InitProxy());
    Set(core_, "script", InitScript());
    Set(core_, "socket", InitSocket());
    Set(core_, "http", InitHTTP());

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
    SocketScope socket_scope(sock_fd);
    Call(core_, "main", socket_scope.GetSocket());
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
        ExecutionGuard guard;
        value = function->Call(object, 1, &arg);
    }
    access_ptr = 0;
    if (TimedOut()) {
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
    if (!RolledBack())
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
                 const string& media_path,
                 DB& db)
    : pimpl_(new Impl(place,
                      app_code_path,
                      release_code_path,
                      media_path,
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
