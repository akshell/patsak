
// (c) 2010 by Anton Korenyushkin

#include "js-core.h"
#include "js-common.h"
#include "js-fs.h"

#include <boost/foreach.hpp>


using namespace ak;
using namespace v8;
using namespace std;


////////////////////////////////////////////////////////////////////////////////
// InitCore
////////////////////////////////////////////////////////////////////////////////

namespace
{
    string app_path;
    string release_path;


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


    string GetFullPath(const Arguments& args)
    {
        CheckArgsLength(args, 1);
        string base_path, path;
        if (args.Length() == 1) {
            base_path = app_path;
            path = Stringify(args[0]);
        } else {
            string app_name = Stringify(args[0]);
            BOOST_FOREACH(char c, app_name)
                if (!((c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') ||
                      c == '-'))
                    throw Error(Error::VALUE, "Invalid app name");
            base_path = release_path + '/' + app_name;
            path = Stringify(args[1]);
        }
        if (GetPathDepth(path) <= 0)
            throw Error(Error::PATH, "Code path \"" + path + "\" is illegal");
        return base_path + '/' + path;
    }


    DEFINE_JS_CALLBACK(ReadCodeCb, args)
    {
        auto_ptr<Chars> data_ptr(ReadFile(GetFullPath(args)));
        return String::New(&data_ptr->front(), data_ptr->size());
    }


    DEFINE_JS_CALLBACK(GetCodeModDateCb, args)
    {
        time_t date = GetStat(GetFullPath(args))->st_mtime;
        return Date::New(static_cast<double>(date) * 1000);
    }
}


Handle<Object> ak::InitCore(const string& app_path, const string& release_path)
{
    ::app_path = app_path;
    ::release_path = release_path;
    Handle<Object> result(Object::New());
    SetFunction(result, "print", PrintCb);
    SetFunction(result, "set", SetCb);
    SetFunction(result, "hash", HashCb);
    SetFunction(result, "readCode", ReadCodeCb);
    SetFunction(result, "getCodeModDate", GetCodeModDateCb);
    return result;
}
