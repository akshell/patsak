
// (c) 2010 by Anton Korenyushkin

#include "js-script.h"
#include "js-common.h"


using namespace ak;
using namespace v8;
using namespace std;


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


DEFINE_JS_CONSTRUCTOR(ScriptBg, "Script", ConstructorCb,
                      /*object_template*/, proto_template)
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
// InitScript
////////////////////////////////////////////////////////////////////////////////

Handle<Object> ak::InitScript()
{
    Handle<Object> result(Object::New());
    Set(result, "Script", ScriptBg::GetJSClass().GetFunction());
    return result;
}
