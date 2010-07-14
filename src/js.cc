
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
// API
////////////////////////////////////////////////////////////////////////////////

// File generated from init.js script by xxd -i, INIT_JS is defined here
#include "init.js.h"


namespace
{
    const int MAX_YOUNG_SPACE_SIZE =  2 * 1024 * 1024;
    const int MAX_OLD_SPACE_SIZE   = 32 * 1024 * 1024;
    const int STACK_LIMIT          =  2 * 1024 * 1024;

    bool initialized = false;
    Persistent<Context> context;
    jmp_buf environment;


    void HandleFatalError(const char* location, const char* message)
    {
        if (string(message) != "Allocation failed - process out of memory")
            Fail(string("Fatal error in ") + location + ": " + message);
        longjmp(environment, 1);
    }


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



    bool Run(Handle<Function> function,
             Handle<v8::Value> arg,
             int out_fd,
             bool print_ok)
    {
        if (setjmp(environment)) {
            Write(out_fd, "ERROR\n<Out of memory>");
            return false;
        }
        TryCatch try_catch;
        Handle<v8::Value> value;
        {
            ExecutionGuard guard;
            value = function->Call(context->Global(), 1, &arg);
        }
        if (value.IsEmpty()) {
            RollBack();
            if (TimedOut()) {
                Write(out_fd, "ERROR\n<Timed out>");
            } else if (out_fd != -1) {
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
        if (RolledBack())
            RollBack();
        else
            Commit();
        if (out_fd != -1 && print_ok) {
            Write(out_fd, "OK\n");
            Binarizator binarizator(value);
            Write(out_fd, binarizator.GetData(), binarizator.GetSize());
        }
        return true;
    }


    void Call(const string& func_name, Handle<v8::Value> arg, int out_fd = -1)
    {
        if (!initialized) {
            Handle<Function> require_func(
                Handle<Function>::Cast(Get(context->Global(), "require")));
            AK_ASSERT(!require_func.IsEmpty());
            if (!Run(require_func, String::New("main"), out_fd, false))
                return;
            initialized = true;
        }
        Handle<v8::Value> func_value(Get(context->Global(), func_name));
        if (func_value.IsEmpty() || !func_value->IsFunction())
            Write(out_fd, "ERROR\n" + func_name + " is not a function");
        else
            Run(Handle<Function>::Cast(func_value), arg, out_fd, true);
    }
}


void ak::HandleRequest(int sock_fd)
{
    HandleScope handle_scope;
    Context::Scope context_scope(context);
    SocketScope socket_scope(sock_fd);
    Call("main", socket_scope.GetSocket());
}


void ak::EvalExpr(const Chars& expr, int out_fd)
{
    HandleScope handle_scope;
    Context::Scope context_scope(context);
    Handle<v8::Value> arg(String::New(&expr[0], expr.size()));
    Call("eval", arg, out_fd);
}


bool ak::ProgramIsDead()
{
    return V8::IsDead();
}


void ak::InitJS(const string& code_path,
                const string& lib_path,
                const string& media_path,
                const string& git_path_prefix,
                const string& git_path_suffix,
                const string& db_options,
                const string& schema_name,
                const string& tablespace_name)
{
    InitDatabase(db_options, schema_name, tablespace_name);

    V8::SetFatalErrorHandler(HandleFatalError);

    ResourceConstraints rc;
    rc.set_max_young_space_size(MAX_YOUNG_SPACE_SIZE);
    rc.set_max_old_space_size(MAX_OLD_SPACE_SIZE);
    rc.set_stack_limit(ComputeStackLimit(STACK_LIMIT));
    bool ret = v8::SetResourceConstraints(&rc);
    AK_ASSERT(ret);

    HandleScope handle_scope;
    context = Context::New();
    Context::Scope context_scope(context);
    Handle<Object> basis(Object::New());
    Set(basis, "core", InitCore());
    Set(basis, "db", InitDB());
    Set(basis, "fs", InitFS(code_path, lib_path, media_path));
    Set(basis, "binary", InitBinary());
    Set(basis, "proxy", InitProxy());
    Set(basis, "script", InitScript());
    Set(basis, "socket", InitSocket());
    Set(basis, "http", InitHTTP());
    if (!git_path_prefix.empty() || !git_path_suffix.empty())
        Set(basis, "git", InitGit(git_path_prefix, git_path_suffix));
    Handle<Array> error_classes(Array::New());
    InitErrorClasses(error_classes);

    Handle<Script> script(
        Script::Compile(String::New(INIT_JS, sizeof(INIT_JS)),
                        String::New("native init.js")));
    AK_ASSERT(!script.IsEmpty());
    Handle<v8::Value> init_func_value(script->Run());
    AK_ASSERT(!init_func_value.IsEmpty() && init_func_value->IsFunction());
    Handle<Function> init_func(Handle<Function>::Cast(init_func_value));
    Handle<v8::Value> args[] = {basis, error_classes};
    Handle<v8::Value> init_ret(init_func->Call(context->Global(), 2, args));
    AK_ASSERT(!init_ret.IsEmpty());
}
