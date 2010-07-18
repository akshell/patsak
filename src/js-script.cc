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
        DECLARE_JS_CONSTRUCTOR(ScriptBg);

        ScriptBg(Handle<Script> script);
        ~ScriptBg();

    private:
        Persistent<Script> script_;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, RunCb,
                             const Arguments&) const;
    };
}


DEFINE_JS_CONSTRUCTOR(ScriptBg, "Script", /*object_template*/, proto_template)
{
    SetFunction(proto_template, "run", RunCb);
}


DEFINE_JS_CONSTRUCTOR_CALLBACK(ScriptBg, args)
{
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
    if (script.IsEmpty())
        throw Propagate();
    return new ScriptBg(script);
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
    PutClass<ScriptBg>(result);
    return result;
}
