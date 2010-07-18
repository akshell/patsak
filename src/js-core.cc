// (c) 2010 by Anton Korenyushkin

#include "js-core.h"
#include "js-common.h"


using namespace ak;
using namespace v8;
using namespace std;


////////////////////////////////////////////////////////////////////////////////
// InitCore
////////////////////////////////////////////////////////////////////////////////

namespace
{
    DEFINE_JS_FUNCTION(PrintCb, args)
    {
        CheckArgsLength(args, 1);
        Log(Stringify(args[0]));
        return Undefined();
    }


    DEFINE_JS_FUNCTION(SetCb, args)
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


    DEFINE_JS_FUNCTION(HashCb, args)
    {
        CheckArgsLength(args, 1);
        int hash = (args[0]->IsObject()
                    ? args[0]->ToObject()->GetIdentityHash()
                    : 0);
        return Integer::New(hash);
    }
}


Handle<Object> ak::InitCore()
{
    Handle<Object> result(Object::New());
    SetFunction(result, "print", PrintCb);
    SetFunction(result, "set", SetCb);
    SetFunction(result, "hash", HashCb);
    return result;
}
