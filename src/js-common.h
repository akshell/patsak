
// (c) 2009-2010 by Anton Korenyushkin

#ifndef JS_COMMON_H
#define JS_COMMON_H

#include "common.h"

#include <v8.h>
#include <boost/utility.hpp>


////////////////////////////////////////////////////////////////////////////////
// Utils
////////////////////////////////////////////////////////////////////////////////

namespace v8
{
    inline PropertyAttribute operator|(v8::PropertyAttribute lhs,
                                       v8::PropertyAttribute rhs)
    {
        return static_cast<v8::PropertyAttribute>(static_cast<int>(lhs) |
                                                  static_cast<int>(rhs));
    }
}


namespace ak
{
    extern v8::Persistent<v8::Object> js_error_classes;


    class Propagate {};


    void ThrowError(const ak::Error& err);
    std::string Stringify(v8::Handle<v8::Value> value);
    void CheckArgsLength(const v8::Arguments& args, int length);
    v8::Handle<v8::Array> GetArray(v8::Handle<v8::Value> value);

    v8::Handle<v8::Value> Get(v8::Handle<v8::Object> object,
                              const std::string& name);


    template <typename OwnerT, typename PropT>
    void Set(v8::Handle<OwnerT> owner,
             const std::string& name,
             v8::Handle<PropT> prop,
             v8::PropertyAttribute attribs = v8::None)
    {
        owner->Set(v8::String::New(name.c_str()), prop, attribs);
    }


    void SetFunction(v8::Handle<v8::ObjectTemplate> object_template,
                     const std::string& name,
                     v8::InvocationCallback callback);

    void SetFunction(v8::Handle<v8::Object> object,
                     const std::string& name,
                     v8::InvocationCallback callback);


    struct Prop {
        v8::Handle<v8::Value> key;
        v8::Handle<v8::Value> value;

        Prop(v8::Handle<v8::Value> key, v8::Handle<v8::Value> value)
            : key(key), value(value) {}
    };


    class PropEnumerator {
    public:
        explicit PropEnumerator(v8::Handle<v8::Object> object)
            : object_(object)
            , keys_(object->GetPropertyNames())
            , size_(keys_->Length()) {}

        size_t GetSize() const {
            return size_;
        }

        Prop GetProp(size_t index) const {
            v8::Handle<v8::Value> key(keys_->Get(v8::Integer::New(index)));
            v8::Handle<v8::Value> value(object_->Get(key));
            if (value.IsEmpty())
                throw Propagate();
            return Prop(key, value);
        }

    private:
        v8::Handle<v8::Object> object_;
        v8::Handle<v8::Array> keys_;
        size_t size_;
    };
}

////////////////////////////////////////////////////////////////////////////////
// ExecutionGuard, CallbackGuard, and TimedOut
////////////////////////////////////////////////////////////////////////////////

namespace ak
{
    struct ExecutionGuard {
        ExecutionGuard();
        ~ExecutionGuard();
    };


    struct CallbackGuard {
        CallbackGuard();
        ~CallbackGuard();
    };


    bool TimedOut();
}

////////////////////////////////////////////////////////////////////////////////
// JSClass
////////////////////////////////////////////////////////////////////////////////

namespace ak
{
    class JSClassBase : boost::noncopyable {
    public:
        std::string GetName() const;
        v8::Handle<v8::ObjectTemplate> GetObjectTemplate() const;
        static void InitConstructors(v8::Handle<v8::Object> holder);
        v8::Handle<v8::Function> GetFunction();

    protected:
        JSClassBase(const std::string& name,
                    JSClassBase* parent_ptr = 0,
                    v8::InvocationCallback constructor = 0);

        ~JSClassBase();

        void* Cast(v8::Handle<v8::Value> value);
        v8::Handle<v8::ObjectTemplate> GetProtoTemplate() const;

    private:
        std::string name_;
        v8::Persistent<v8::FunctionTemplate> function_template_;
        v8::Persistent<v8::Function> function_;
        v8::Persistent<v8::TypeSwitch> type_switch_;
        std::vector<v8::Handle<v8::FunctionTemplate> > cast_js_classes_;

        static std::vector<JSClassBase*>& GetInstancePtrs();
    };


    template <typename T>
    class JSClass : public JSClassBase {
    public:
        JSClass(const std::string& name,
                JSClassBase* parent_ptr = 0,
                v8::InvocationCallback constructor = 0);

        v8::Handle<v8::Object> Instantiate(T* bg_ptr);
        void Attach(v8::Handle<v8::Object> object, T* bg_ptr);
        T* Cast(v8::Handle<v8::Value> value);

    private:
        static void DeleteCb(v8::Persistent<v8::Value> object,
                             void* parameter);
    };
}


template <typename T>
ak::JSClass<T>::JSClass(const std::string& name,
                        JSClassBase* parent_ptr,
                        v8::InvocationCallback constructor)
    : JSClassBase(name, parent_ptr, constructor)
{
    T::AdjustTemplates(GetObjectTemplate(), GetProtoTemplate());
}


template <typename T>
v8::Handle<v8::Object> ak::JSClass<T>::Instantiate(T* bg_ptr)
{
    v8::Handle<v8::Object> result(GetFunction()->NewInstance());
    Attach(result, bg_ptr);
    return result;
}


template <typename T>
void ak::JSClass<T>::Attach(v8::Handle<v8::Object> object, T* bg_ptr)
{
    v8::Persistent<v8::Object>::New(object).MakeWeak(bg_ptr, DeleteCb);
    object->SetInternalField(0, v8::External::New(bg_ptr));
}


template <typename T>
T* ak::JSClass<T>::Cast(v8::Handle<v8::Value> value)
{
    return static_cast<T*>(JSClassBase::Cast(value));
}


template <typename T>
void ak::JSClass<T>::DeleteCb(v8::Persistent<v8::Value> object, void* parameter)
{
    delete static_cast<T*>(parameter);
    object.Dispose();
    object.Clear();
}

////////////////////////////////////////////////////////////////////////////////
// JSClass helpers
////////////////////////////////////////////////////////////////////////////////

namespace ak
{
    template <typename T>
    v8::Handle<v8::Object> JSNew()
    {
        return T::GetJSClass().Instantiate(new T());
    }


    template <typename T, typename T1>
    v8::Handle<v8::Object> JSNew(T1 arg1)
    {
        return T::GetJSClass().Instantiate(new T(arg1));
    }


    template <typename T, typename T1, typename T2>
    v8::Handle<v8::Object> JSNew(T1 arg1, T2 arg2)
    {
        return T::GetJSClass().Instantiate(new T(arg1, arg2));
    }


    template <typename T, typename T1, typename T2, typename T3>
    v8::Handle<v8::Object> JSNew(T1 arg1, T2 arg2, T3 arg3)
    {
        return T::GetJSClass().Instantiate(new T(arg1, arg2, arg3));
    }


    template <typename T, typename T1, typename T2, typename T3, typename T4>
    v8::Handle<v8::Object> JSNew(T1 arg1, T2 arg2, T3 arg3, T4 arg4)
    {
        return T::GetJSClass().Instantiate(new T(arg1, arg2, arg3, arg4));
    }


    template <typename T>
    T& GetBg(v8::Handle<v8::Value> holder)
    {
        JSClass<T>& js_class(T::GetJSClass());
        T* bg_ptr = js_class.Cast(holder);
        if (!bg_ptr)
            throw ak::Error(ak::Error::TYPE,
                            js_class.GetName() + " object was expected");
        return *bg_ptr;
    }
}


#define JS_CATCH(T)                                                     \
    catch (const ak::Propagate& err) {                                  \
        return T();                                                     \
    } catch (const ak::Error& err) {                                    \
        ak::ThrowError(err);                                            \
        return T();                                                     \
    } catch (const std::exception& err) {                               \
        ak::Fail(err.what());                                           \
    }


#define DECLARE_JS_CLASS(cls)                                           \
    static ak::JSClass<cls>& GetJSClass();                              \
    static void AdjustTemplates(v8::Handle<v8::ObjectTemplate>,         \
                                v8::Handle<v8::ObjectTemplate>)


#define DECLARE_JS_CONSTRUCTOR(cls)                                     \
    private:                                                            \
    static v8::Handle<v8::Value> ConstructorCb(const v8::Arguments&);   \
    static cls* ConstructorCbImpl(const v8::Arguments&);                \
    public:                                                             \
    DECLARE_JS_CLASS(cls)


#define DO_DEFINE_JS_SUBCONSTRUCTOR(cls, name, parent_ptr, constructor, \
                                    object_template, proto_template)    \
    ak::JSClass<cls>& cls::GetJSClass() {                               \
        static ak::JSClass<cls> result(name, parent_ptr, constructor);  \
        return result;                                                  \
    }                                                                   \
    void cls::AdjustTemplates(                                          \
        v8::Handle<v8::ObjectTemplate> object_template,                 \
        v8::Handle<v8::ObjectTemplate> proto_template)


#define DEFINE_JS_SUBCONSTRUCTOR(cls, name, parent,                     \
                                 object_template, proto_template)       \
    DO_DEFINE_JS_SUBCONSTRUCTOR(                                        \
        cls, name, &parent::GetJSClass(), ConstructorCb,                \
        object_template, proto_template)


#define DEFINE_JS_SUBCLASS(cls, name, parent,                           \
                           object_template, proto_template)             \
    DO_DEFINE_JS_SUBCONSTRUCTOR(                                        \
        cls, name, &parent::GetJSClass(), 0,                            \
        object_template, proto_template)


#define DEFINE_JS_CONSTRUCTOR(cls, name,                                \
                              object_template, proto_template)          \
    DO_DEFINE_JS_SUBCONSTRUCTOR(                                        \
        cls, name, 0, ConstructorCb, object_template, proto_template)


#define DEFINE_JS_CLASS(cls, name, object_template, proto_template)     \
    DO_DEFINE_JS_SUBCONSTRUCTOR(                                        \
        cls, name, 0, 0, object_template, proto_template)


#define DECLARE_JS_CALLBACK1(T, name, T1)                     \
    static T name(T1);                                        \
    T name##Impl(T1)


#define DECLARE_JS_CALLBACK2(T, name, T1, T2)   \
    static T name(T1, T2);                      \
    T name##Impl(T1, T2)


#define DECLARE_JS_CALLBACK3(T, name, T1, T2, T3)        \
    static T name(T1, T2, T3);                           \
    T name##Impl(T1, T2, T3)


#define JS_CALLBACK_GUARD(T)                        \
    ak::CallbackGuard callback_guard__;             \
    if (ak::TimedOut())                             \
        return T()


#define DEFINE_JS_FUNCTION(name, args)                      \
    v8::Handle<v8::Value> name##Impl(const Arguments&);     \
    v8::Handle<v8::Value> name(const Arguments& a)          \
    {                                                       \
        JS_CALLBACK_GUARD(v8::Handle<v8::Value>);           \
        try {                                               \
            return name##Impl(a);                           \
        } JS_CATCH(v8::Handle<v8::Value>);                  \
    }                                                       \
    v8::Handle<v8::Value> name##Impl(const Arguments& args)



#define DEFINE_JS_CALLBACK1(T, cls, name, T1, arg1)                     \
    T cls::name(T1 a1)                                                  \
    {                                                                   \
        JS_CALLBACK_GUARD(T);                                           \
        try {                                                           \
            return ak::GetBg<cls>(a1.Holder()).name##Impl(a1);          \
        } JS_CATCH(T);                                                  \
    }                                                                   \
    T cls::name##Impl(T1 arg1)


#define DEFINE_JS_CALLBACK2(T, cls, name, T1, arg1, T2, arg2)           \
    T cls::name(T1 a1, T2 a2)                                           \
    {                                                                   \
        JS_CALLBACK_GUARD(T);                                           \
        try {                                                           \
            return ak::GetBg<cls>(a2.Holder()).name##Impl(a1, a2);      \
        } JS_CATCH(T);                                                  \
    }                                                                   \
    T cls::name##Impl(T1 arg1, T2 arg2)


#define DEFINE_JS_CALLBACK3(T, cls, name, T1, arg1, T2, arg2, T3, arg3) \
    T cls::name(T1 a1, T2 a2, T3 a3)                                    \
    {                                                                   \
        JS_CALLBACK_GUARD(T);                                           \
        try {                                                           \
            return ak::GetBg<cls>(a3.Holder()).name##Impl(a1, a2, a3);  \
        } JS_CATCH(T);                                                  \
    }                                                                   \
    T cls::name##Impl(T1 arg1, T2 arg2, T3 arg3)


#define DEFINE_JS_CONSTRUCTOR_CALLBACK(cls, args)                       \
    v8::Handle<v8::Value> cls::ConstructorCb(const v8::Arguments& a)    \
    {                                                                   \
        if (!a.IsConstructCall())                                       \
            return Undefined();                                         \
        try {                                                           \
            cls::GetJSClass().Attach(a.This(), ConstructorCbImpl(a));   \
            return v8::Handle<v8::Value>();                             \
        } JS_CATCH(Handle<v8::Value>);                                  \
    }                                                                   \
    cls* cls::ConstructorCbImpl(const v8::Arguments& args)

#endif // JS_COMMON_H
