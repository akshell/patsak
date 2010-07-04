
// (c) 2009-2010 by Anton Korenyushkin

#include "js.h"
#include "js-common.h"
#include "js-core.h"
#include "js-db.h"
#include "js-fs.h"
#include "js-binary.h"
#include "js-proxy.h"
#include "js-script.h"
#include "js-socket.h"
#include "js-http.h"
#include "js-git.h"
#include "db.h"

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
         const string& code_path,
         const string& media_path,
         const string& git_path_prefix,
         const string& git_path_suffix,
         DB& db);

    ~Impl();

    void Process(int sock_fd);
    void Eval(const Chars& expr, int out_fd);
    bool IsDead() const;

private:
    bool initialized_;
    DB& db_;
    Persistent<Context> context_;

    bool Run(Handle<Function> function,
             Handle<v8::Value> arg,
             int out_fd,
             bool print_ok);

    void Call(const string& func_name,
              Handle<v8::Value> arg,
              int out_fd = -1);
};


Program::Impl::Impl(const Place& place,
                    const string& code_path,
                    const string& media_path,
                    const string& git_path_prefix,
                    const string& git_path_suffix,
                    DB& db)
    : initialized_(false)
    , db_(db)
{
    V8::SetFatalErrorHandler(HandleFatalError);

    ResourceConstraints rc;
    rc.set_max_young_space_size(MAX_YOUNG_SPACE_SIZE);
    rc.set_max_old_space_size(MAX_OLD_SPACE_SIZE);
    rc.set_stack_limit(ComputeStackLimit(STACK_LIMIT));
    bool ret = v8::SetResourceConstraints(&rc);
    AK_ASSERT(ret);

    HandleScope handle_scope;
    context_ = Context::New();
    Context::Scope context_scope(context_);
    Handle<Object> global(context_->Global());
    Set(global, "app", String::New(place.app_name.c_str()));
    if (!place.spot_name.empty()) {
        Set(global, "spot", String::New(place.spot_name.c_str()));
        Set(global, "owner", String::New(place.owner_name.c_str()));
    }
    Set(global, "core", InitCore(code_path));
    Set(global, "db", InitDB());
    Set(global, "fs", InitFS(media_path));
    Set(global, "binary", InitBinary());
    Set(global, "proxy", InitProxy());
    Set(global, "script", InitScript());
    Set(global, "socket", InitSocket());
    Set(global, "http", InitHTTP());
    if (!git_path_prefix.empty() || !git_path_suffix.empty())
        Set(global, "git", InitGit(git_path_prefix, git_path_suffix));

    // Run init.js script
    Handle<Script> script(Script::Compile(String::New(INIT_JS,
                                                      sizeof(INIT_JS)),
                                          String::New("native init.js")));
    AK_ASSERT(!script.IsEmpty());
    Handle<v8::Value> init_ret(script->Run());
    AK_ASSERT(!init_ret.IsEmpty());

    js_error_classes = Persistent<Object>::New(
        Get(global, "errors")->ToObject()->Clone());
}


Program::Impl::~Impl()
{
    context_.Dispose();
}


void Program::Impl::Eval(const Chars& expr, int out_fd)
{
    HandleScope handle_scope;
    Context::Scope context_scope(context_);
    Handle<v8::Value> arg(String::New(&expr[0], expr.size()));
    Call("eval", arg, out_fd);
}


void Program::Impl::Process(int sock_fd)
{
    HandleScope handle_scope;
    Context::Scope context_scope(context_);
    SocketScope socket_scope(sock_fd);
    Call("main", socket_scope.GetSocket());
}


bool Program::Impl::IsDead() const
{
    return V8::IsDead();
}


bool Program::Impl::Run(Handle<Function> function,
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
        value = function->Call(context_->Global(), 1, &arg);
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


void Program::Impl::Call(const string& func_name,
                         Handle<v8::Value> arg,
                         int out_fd)
{
    if (!initialized_) {
        Handle<Function> require_func(
            Handle<Function>::Cast(Get(context_->Global(), "require")));
        AK_ASSERT(!require_func.IsEmpty());
        if (!Run(require_func, String::New("main"), out_fd, false))
            return;
        initialized_ = true;
    }
    Handle<v8::Value> func_value(Get(context_->Global(), func_name));
    if (func_value.IsEmpty() || !func_value->IsFunction())
        Write(out_fd, "ERROR\n" + func_name + " is not a function");
    else
        Run(Handle<Function>::Cast(func_value), arg, out_fd, true);
}

////////////////////////////////////////////////////////////////////////////////
// Program
////////////////////////////////////////////////////////////////////////////////

Program::Program(const Place& place,
                 const string& code_path,
                 const string& media_path,
                 const string& git_path_prefix,
                 const string& git_path_suffix,
                 DB& db)
    : pimpl_(new Impl(place,
                      code_path,
                      media_path,
                      git_path_prefix,
                      git_path_suffix,
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
