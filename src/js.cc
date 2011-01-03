// (c) 2009-2011 by Anton Korenyushkin

#include "js.h"
#include "js-common.h"
#include "js-core.h"
#include "js-db.h"
#include "js-fs.h"
#include "js-binary.h"
#include "js-proxy.h"
#include "js-script.h"
#include "js-socket.h"
#include "js-http-parser.h"
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

    Persistent<Context> context;
    Persistent<Object> main_exports;
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


    bool Run(const Handle<Object>& holder,
             const string& func_name,
             Handle<v8::Value> arg,
             string* result_ptr = 0)
    {
        if (setjmp(environment)) {
            if (result_ptr)
                *result_ptr = "<Out of memory>";
            return false;
        }
        TryCatch try_catch;
        Handle<v8::Value> ret;
        if (main_exports.IsEmpty()) {
            Handle<Function> require_func(
                Handle<Function>::Cast(Get(context->Global(), "require")));
            AK_ASSERT(!require_func.IsEmpty());
            Handle<v8::Value> main_name(String::New("main"));
            {
                ExecutionGuard guard;
                ret = require_func->Call(context->Global(), 1, &main_name);
            }
            if (!ret.IsEmpty())
                main_exports = Persistent<Object>::New(ret->ToObject());
        }
        if (!main_exports.IsEmpty()) {
            Handle<v8::Value> func_value(Get(holder, func_name));
            if (func_value.IsEmpty() || !func_value->IsFunction()) {
                if (result_ptr)
                    *result_ptr = func_name + " is not a function";
                return false;
            } else {
                ExecutionGuard guard;
                ret = Handle<Function>::Cast(func_value)->Call(
                    context->Global(), 1, &arg);
            }
        }
        if (!ret.IsEmpty()) {
            if (RolledBack())
                RollBack();
            else
                Commit();
            if (result_ptr)
                *result_ptr = Stringify(ret);
            return true;
        }
        RollBack();
        if (!result_ptr)
            return false;
        if (TimedOut()) {
            *result_ptr = "<Timed out>";
            return false;
        }
        Handle<v8::Value> stack_trace(try_catch.StackTrace());
        if (!stack_trace.IsEmpty()) {
            *result_ptr = Stringify(stack_trace);
            return false;
        }
        Handle<Message> message(try_catch.Message());
        if (message.IsEmpty()) {
            Handle<v8::Value> exception(try_catch.Exception());
            AK_ASSERT(!exception.IsEmpty());
            *result_ptr = Stringify(exception);
            return false;
        }
        ostringstream oss;
        oss << Stringify(message->Get()) << "\n    at ";
        Handle<v8::Value> resource_name(message->GetScriptResourceName());
        if (!resource_name->IsUndefined())
            oss << Stringify(resource_name) << ':';
        oss << message->GetLineNumber() << ':' << message->GetStartColumn();
        *result_ptr = oss.str();
        return false;
    }
}


bool ak::HandleRequest(int conn_fd)
{
    HandleScope handle_scope;
    Context::Scope context_scope(context);
    SocketScope socket_scope(conn_fd);
    return Run(main_exports, "handle", socket_scope.GetSocket());
}


bool ak::EvalExpr(const char* expr, size_t size, string& result)
{
    HandleScope handle_scope;
    Context::Scope context_scope(context);
    return Run(context->Global(), "eval", String::New(expr, size), &result);
}


bool ak::ProgramIsDead()
{
    return V8::IsDead();
}


void ak::InitJS(const string& code_path,
                const string& lib_path,
                const GitPathPatterns& git_path_patterns,
                const string& repo_name,
                const string& db_options,
                const string& schema_name,
                const string& tablespace_name,
                bool managed)
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
    Set(basis, "core", InitCore(managed));
    Set(basis, "db", InitDB());
    Set(basis, "fs", InitFS(code_path, lib_path));
    Set(basis, "binary", InitBinary());
    Set(basis, "proxy", InitProxy());
    Set(basis, "script", InitScript());
    Set(basis, "socket", InitSocket());
    Set(basis, "http-parser", InitHttpParser());
    if (!git_path_patterns.empty())
        Set(basis, "git", InitGit(git_path_patterns));
    Handle<Array> error_classes(Array::New());
    InitErrorClasses(error_classes);

    Handle<Script> script(
        Script::Compile(String::New(INIT_JS, sizeof(INIT_JS)),
                        String::New("native init.js")));
    AK_ASSERT(!script.IsEmpty());
    Handle<v8::Value> init_func_value(script->Run());
    AK_ASSERT(!init_func_value.IsEmpty() && init_func_value->IsFunction());
    Handle<Function> init_func(Handle<Function>::Cast(init_func_value));
    Handle<v8::Value> args[] = {
        basis, error_classes, String::New(repo_name.c_str())
    };
    Handle<v8::Value> init_ret(init_func->Call(context->Global(), 3, args));
    AK_ASSERT(!init_ret.IsEmpty());
}
