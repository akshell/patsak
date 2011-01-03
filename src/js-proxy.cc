// (c) 2010-2011 by Anton Korenyushkin

#include "js-proxy.h"
#include "js-common.h"


using namespace ak;
using namespace v8;
using namespace std;


////////////////////////////////////////////////////////////////////////////////
// ProxyBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class ProxyBg {
    public:
        DECLARE_JS_CONSTRUCTOR(ProxyBg);

        ProxyBg(Handle<Object> handler);
        ~ProxyBg();

    private:
        Persistent<Object> handler_;

        Handle<v8::Value> Call(const string& name,
                               int argc,
                               Handle<v8::Value> argv[]) const;

        Handle<Boolean> CallIndicator(const string& name,
                                      Handle<v8::Value> arg) const;

        DECLARE_JS_CALLBACK2(Handle<v8::Value>, GetNamedCb,
                             Local<String>, const AccessorInfo&) const;

        DECLARE_JS_CALLBACK3(Handle<v8::Value>, SetNamedCb,
                             Local<String>,
                             Local<v8::Value>,
                             const AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(Handle<Boolean>, QueryNamedCb,
                             Local<String>, const AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(Handle<Boolean>, DeleteNamedCb,
                             Local<String>, const AccessorInfo&) const;

        DECLARE_JS_CALLBACK1(Handle<Array>, EnumCb,
                             const AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(Handle<v8::Value>, GetIndexedCb,
                             uint32_t, const AccessorInfo&) const;

        DECLARE_JS_CALLBACK3(Handle<v8::Value>, SetIndexedCb,
                             uint32_t,
                             Local<v8::Value>,
                             const AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(Handle<Boolean>, QueryIndexedCb,
                             uint32_t, const AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(Handle<Boolean>, DeleteIndexedCb,
                             uint32_t, const AccessorInfo&) const;
    };
}


DEFINE_JS_CONSTRUCTOR(ProxyBg, "Proxy", object_template, /*proto_template*/)
{
    object_template->SetNamedPropertyHandler(GetNamedCb,
                                             SetNamedCb,
                                             QueryNamedCb,
                                             DeleteNamedCb,
                                             EnumCb);
    object_template->SetIndexedPropertyHandler(GetIndexedCb,
                                               SetIndexedCb,
                                               QueryIndexedCb,
                                               DeleteIndexedCb);
}


DEFINE_JS_CONSTRUCTOR_CALLBACK(ProxyBg, args)
{
    CheckArgsLength(args, 1);
    if (!args[0]->IsObject())
        throw Error(Error::TYPE, "Object required");
    return new ProxyBg(args[0]->ToObject());
}


ProxyBg::ProxyBg(Handle<Object> handler)
    : handler_(Persistent<Object>::New(handler))
{
}


ProxyBg::~ProxyBg()
{
    handler_.Dispose();
}


Handle<v8::Value> ProxyBg::Call(const string& name,
                                int argc,
                                Handle<v8::Value> argv[]) const
{
    Handle<v8::Value> func(Get(handler_, name));
    if (func.IsEmpty())
        return Handle<v8::Value>();
    if (!func->IsFunction())
        throw Error(Error::TYPE, name + " is not a function");
    return Handle<Function>::Cast(func)->Call(handler_, argc, argv);
}


Handle<Boolean> ProxyBg::CallIndicator(const string& name,
                                  Handle<v8::Value> arg) const
{
    TryCatch try_catch;
    Handle<v8::Value> value(Call(name, 1, &arg));
    return value.IsEmpty() ? Boolean::New(false) : value->ToBoolean();
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, ProxyBg, GetNamedCb,
                    Local<String>, property,
                    const AccessorInfo&, /*info*/) const
{
    Handle<v8::Value> arg(property);
    return Call("get", 1, &arg);
}


DEFINE_JS_CALLBACK3(Handle<v8::Value>, ProxyBg, SetNamedCb,
                    Local<String>, property,
                    Local<v8::Value>, value,
                    const AccessorInfo&, /*info*/) const
{
    Handle<v8::Value> argv[] = {property, value};
    return Call("set", 2, argv);
}


DEFINE_JS_CALLBACK2(Handle<Boolean>, ProxyBg, QueryNamedCb,
                    Local<String>, property,
                    const AccessorInfo&, /*info*/) const
{
    return CallIndicator("query", property);
}


DEFINE_JS_CALLBACK2(Handle<Boolean>, ProxyBg, DeleteNamedCb,
                    Local<String>, property,
                    const AccessorInfo&, /*info*/) const
{
    return CallIndicator("del", property);
}


DEFINE_JS_CALLBACK1(Handle<Array>, ProxyBg, EnumCb,
                    const AccessorInfo&, /*info*/) const
{
    TryCatch try_catch;
    Handle<v8::Value> ret(Call("list", 0, 0));
    if (ret.IsEmpty())
        return Handle<Array>();
    if (!ret->IsObject())
        return Array::New();
    Handle<Object> object(ret->ToObject());
    Handle<v8::Value> length_value(Get(object, "length"));
    if (length_value.IsEmpty())
        return Handle<Array>();
    if (!length_value->IsInt32())
        return Array::New();
    int32_t length = length_value->Int32Value();
    Handle<Array> result(Array::New(length > 0 ? length : 0));
    for (int32_t index = 0; index < length; ++index) {
        Handle<Integer> index_value(Integer::New(index));
        Handle<v8::Value> item(object->Get(index_value));
        if (item.IsEmpty())
            return Handle<Array>();
        result->Set(index_value, item);
    }
    return result;
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, ProxyBg, GetIndexedCb,
                    uint32_t, index,
                    const AccessorInfo&, /*info*/) const
{
    Handle<v8::Value> arg(Integer::New(index));
    return Call("get", 1, &arg);
}


DEFINE_JS_CALLBACK3(Handle<v8::Value>, ProxyBg, SetIndexedCb,
                    uint32_t, index,
                    Local<v8::Value>, value,
                    const AccessorInfo&, /*info*/) const
{
    Handle<v8::Value> argv[] = {Integer::New(index), value};
    return Call("set", 2, argv);
}


DEFINE_JS_CALLBACK2(Handle<Boolean>, ProxyBg, QueryIndexedCb,
                    uint32_t, index,
                    const AccessorInfo&, /*info*/) const
{
    return CallIndicator("query", Integer::New(index));
}


DEFINE_JS_CALLBACK2(Handle<Boolean>, ProxyBg, DeleteIndexedCb,
                    uint32_t, index,
                    const AccessorInfo&, /*info*/) const
{
    return CallIndicator("del", Integer::New(index));
}

////////////////////////////////////////////////////////////////////////////////
// InitProxy
////////////////////////////////////////////////////////////////////////////////

Handle<Object> ak::InitProxy()
{
    Handle<Object> result(Object::New());
    PutClass<ProxyBg>(result);
    return result;
}
