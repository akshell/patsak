
// (c) 2010 by Anton Korenyushkin

#include "js-core.h"
#include "js-common.h"
#include "js-fs.h"


using namespace ak;
using namespace v8;
using namespace std;


////////////////////////////////////////////////////////////////////////////////
// InitCore
////////////////////////////////////////////////////////////////////////////////

namespace
{
    string code_path;


    DEFINE_JS_CALLBACK(PrintCb, args)
    {
        CheckArgsLength(args, 1);
        Log(Stringify(args[0]));
        return Undefined();
    }


    DEFINE_JS_CALLBACK(SetCb, args)
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


    DEFINE_JS_CALLBACK(HashCb, args)
    {
        CheckArgsLength(args, 1);
        int hash = (args[0]->IsObject()
                    ? args[0]->ToObject()->GetIdentityHash()
                    : 0);
        return Integer::New(hash);
    }


    string GetPath(const Arguments& args)
    {
        CheckArgsLength(args, 1);
        string path(Stringify(args[0]));
        if (GetPathDepth(path) <= 0)
            throw Error(Error::PATH, "Code path \"" + path + "\" is illegal");
        return code_path + '/' + path;
    }


    DEFINE_JS_CALLBACK(ReadCodeCb, args)
    {
        auto_ptr<Chars> data_ptr(ReadFile(GetPath(args)));
        return String::New(&data_ptr->front(), data_ptr->size());
    }


    DEFINE_JS_CALLBACK(GetCodeModDateCb, args)
    {
        time_t date = GetStat(GetPath(args))->st_mtime;
        return Date::New(static_cast<double>(date) * 1000);
    }
}


Handle<Object> ak::InitCore(const string& code_path)
{
    ::code_path = code_path;
    Handle<Object> result(Object::New());
    SetFunction(result, "print", PrintCb);
    SetFunction(result, "set", SetCb);
    SetFunction(result, "hash", HashCb);
    SetFunction(result, "readCode", ReadCodeCb);
    SetFunction(result, "getCodeModDate", GetCodeModDateCb);
    return result;
}
