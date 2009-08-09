
// (c) 2009 by Anton Korenyushkin

/// \file js-common.h
/// General JavaScript helpers

#ifndef JS_COMMON_H
#define JS_COMMON_H

#include "error.h"

#include <v8.h>
#include <boost/utility.hpp>
#include <boost/lexical_cast.hpp>

#include <memory>
#include <vector>
#include <map>
#include <stdlib.h>


////////////////////////////////////////////////////////////////////////////////
// Stuff
////////////////////////////////////////////////////////////////////////////////

/// Throw JavaScript exception. Don't forget to return immediately
#define JS_THROW(type, message)                                         \
    v8::ThrowException(                                                 \
        v8::Exception::type(v8::String::New(std::string(message).c_str())))


namespace v8
{
    /// Just handy
    inline PropertyAttribute operator|(v8::PropertyAttribute lhs,
                                       v8::PropertyAttribute rhs)
    {
        return static_cast<v8::PropertyAttribute>(static_cast<int>(lhs) |
                                                  static_cast<int>(rhs));
    }
}


namespace ku
{
    /// Transform v8::Value into std::string
    inline std::string Stringify(v8::Handle<v8::Value> value)
    {
        v8::String::Utf8Value utf8_value(value);
        return std::string(*utf8_value, utf8_value.length());
    }


    inline int32_t GetArrayLikeLength(v8::Handle<v8::Value> value)
    {
        if (!value->IsObject())
            return -1;
        v8::Handle<v8::Object> object(value->ToObject());
        v8::Handle<v8::String> length_string(v8::String::NewSymbol("length"));
        if (!object->Has(length_string))
            return -1;
        v8::Handle<v8::Value> length_value(object->Get(length_string));
        if (!length_value->IsInt32())
            return -1;
        int32_t result = length_value->ToInt32()->Value();
        if (result < 0)
            return -1;
        return result;
    }


    inline v8::Handle<v8::Value> GetArrayLikeItem(v8::Handle<v8::Value> value,
                                                  int32_t index)
    {
        v8::Handle<v8::Object> object(value->ToObject());
        KU_ASSERT(!object.IsEmpty());
        v8::Handle<v8::Value> index_value(v8::Integer::New(index));
        if (!object->Has(index_value->ToString())) {
            JS_THROW(Error, "Bad array like object");
            return v8::Handle<v8::Value>();
        }
        v8::Handle<v8::Value> result(object->Get(index_value));
        return result;
    }


    struct Prop {
        v8::Handle<v8::Value> key;
        v8::Handle<v8::Value> value;

        Prop(v8::Handle<v8::Value> key, v8::Handle<v8::Value> value)
            : key(key), value(value) {}
    };
    

    /// Object property enumerator. Must be only stack-allocated
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
            return Prop(key, object_->Get(key));
        }

    private:
        v8::Handle<v8::Object> object_;
        v8::Handle<v8::Array> keys_;
        size_t size_;
    };
}


#define JS_CHECK_LENGTH(args, length)                                   \
    do {                                                                \
        if (args.Length() != length) {                                  \
            JS_THROW(Error,                                             \
                     "Exactly " +                                       \
                     boost::lexical_cast<string>(length) +              \
                     " arguments required");                            \
            return v8::Handle<v8::Value>();                             \
        }                                                               \
    } while (0)


#define JS_ERROR_CHECK(cond, error, message)                            \
    do {                                                                \
        if (!(cond)) {                                                  \
            JS_THROW(error, message);                                   \
            return v8::Handle<v8::Value>();                             \
        }                                                               \
    } while (0)


#define JS_CHECK(cond, message)                 \
    JS_ERROR_CHECK(cond, Error, message)


#define JS_TYPE_CHECK(cond, message)            \
    JS_ERROR_CHECK(cond, TypeError, message)


#define JS_CAN_THROW(expr)                      \
    do {                                        \
        if (!(expr))                            \
            return v8::Handle<v8::Value>();     \
    } while (0)


#define JS_PROPAGATE(statements)                \
    do {                                        \
        try {                                   \
            statements;                         \
        } catch (const ku::Error& err) {        \
            JS_THROW(Error, err.what());        \
            return Handle<v8::Value>();         \
        }                                       \
    } while (0)

////////////////////////////////////////////////////////////////////////////////
// JSClass
////////////////////////////////////////////////////////////////////////////////

namespace ku
{
    class JSClassBase : boost::noncopyable {
    public:
        v8::Handle<v8::ObjectTemplate> GetObjectTemplate() const;
        static void InitConstructors(v8::Handle<v8::Object> holder);
        
    protected:
        JSClassBase(const std::string& name);
        ~JSClassBase();
        v8::Handle<v8::Function> GetFunction();
        void AddSubClass(const JSClassBase& subclass);
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
    
    
    /// JavaScript class background manager
    template <typename T>
    class JSClass : public JSClassBase {
    public:
        JSClass(const std::string& name);

        v8::Handle<v8::Object> Instantiate(T* bg_ptr);
        T* Cast(v8::Handle<v8::Value> value);
        template<typename U>
        void AddSubClass(const JSClass<U>& subclass);

    private:
        static void DeleteCb(v8::Persistent<v8::Value> object,
                             void* parameter);
    };
}


template <typename T>
ku::JSClass<T>::JSClass(const std::string& name)
    : JSClassBase(name)
{
    T::AdjustTemplates(GetObjectTemplate(), GetProtoTemplate());
}


template <typename T>
v8::Handle<v8::Object> ku::JSClass<T>::Instantiate(T* bg_ptr)
{
    v8::Persistent<v8::Object>
        result(v8::Persistent<v8::Object>::New(GetFunction()->NewInstance()));
    result.MakeWeak(bg_ptr, DeleteCb);
    result->SetInternalField(0, v8::External::New(bg_ptr));
    return result;
}


template <typename T>
T* ku::JSClass<T>::Cast(v8::Handle<v8::Value> value)
{
    return static_cast<T*>(JSClassBase::Cast(value));
}


template <typename T>
template <typename U>
void ku::JSClass<T>::AddSubClass(const JSClass<U>& subclass)
{
    while (false) { *(static_cast<T**>(0)) = static_cast<U*>(0); }
    JSClassBase::AddSubClass(subclass);
}


template <typename T>
void ku::JSClass<T>::DeleteCb(v8::Persistent<v8::Value> object, void* parameter)
{
    delete static_cast<T*>(parameter);
    object.Dispose();
    object.Clear();
}

////////////////////////////////////////////////////////////////////////////////
// JSClass helpers
////////////////////////////////////////////////////////////////////////////////

namespace ku
{
    template <typename T>
    v8::Handle<v8::Object> JSNew()
    {
        return T::GetJSClass().Instantiate(new T());
    }


    template <typename T, typename Arg1T>
    v8::Handle<v8::Object> JSNew(Arg1T arg1)
    {
        return T::GetJSClass().Instantiate(new T(arg1));
    }


    template <typename T, typename Arg1T, typename Arg2T>
    v8::Handle<v8::Object> JSNew(Arg1T arg1, Arg2T arg2)
    {
        return T::GetJSClass().Instantiate(new T(arg1, arg2));
    }


    template <typename T, typename Arg1T, typename Arg2T, typename Arg3T>
    v8::Handle<v8::Object> JSNew(Arg1T arg1, Arg2T arg2, Arg3T arg3)
    {
        return T::GetJSClass().Instantiate(new T(arg1, arg2, arg3));
    }


    template <typename T,
              typename Arg1T,
              typename Arg2T,
              typename Arg3T,
              typename Arg4T>
    v8::Handle<v8::Object> JSNew(Arg1T arg1, Arg2T arg2, Arg3T arg3, Arg4T arg4)
    {
        return T::GetJSClass().Instantiate(new T(arg1, arg2, arg3, arg4));
    }
}


#define DECLARE_JS_CLASS(cls)                                           \
    static ku::JSClass<cls>& GetJSClass();                              \
    static void AdjustTemplates(v8::Handle<v8::ObjectTemplate>,         \
                                v8::Handle<v8::ObjectTemplate>)    


#define DEFINE_JS_CLASS(cls, name, object_template, proto_template)     \
    ku::JSClass<cls>& cls::GetJSClass() {                               \
        static ku::JSClass<cls> js_class__(name);                       \
        return js_class__;                                              \
    }                                                                   \
    void cls::AdjustTemplates(v8::Handle<v8::ObjectTemplate> object_template, \
                              v8::Handle<v8::ObjectTemplate> proto_template)


#define DECLARE_JS_CALLBACK1(ret_type, name, arg_type)               \
    static ret_type name(arg_type);                                  \
    ret_type name##Impl(arg_type)


#define DECLARE_JS_CALLBACK2(ret_type, name, arg1_type, arg2_type)   \
    static ret_type name(arg1_type, arg2_type);                      \
    ret_type name##Impl(arg1_type, arg2_type)


#define DEFINE_JS_CALLBACK1(ret_type, cls, name,                        \
                            arg_type, arg_name)                         \
    ret_type cls::name(arg_type arg)                                    \
    {                                                                   \
        cls* bg_ptr__(cls::GetJSClass().Cast(arg.Holder()));            \
        if (!bg_ptr__) {                                                \
            JS_THROW(Error, "Illegal operation");                       \
            return ret_type();                                          \
        }                                                               \
        try {                                                           \
            return bg_ptr__->name##Impl(arg);                           \
        } catch (const std::exception& err) {                           \
            ku::Fail(err.what());                                       \
        }                                                               \
    }                                                                   \
    ret_type cls::name##Impl(arg_type arg_name)


#define DEFINE_JS_CALLBACK2(ret_type, cls, name,                        \
                            arg1_type, arg1_name,                       \
                            arg2_type, arg2_name)                       \
    ret_type cls::name(arg1_type arg1, arg2_type arg2)                  \
    {                                                                   \
        cls* bg_ptr__(cls::GetJSClass().Cast(arg2.Holder()));           \
        if (!bg_ptr__) {                                                \
            JS_THROW(Error, "Illegal operation");                       \
            return ret_type();                                          \
        }                                                               \
        try {                                                           \
            return bg_ptr__->name##Impl(arg1, arg2);                    \
        } catch (const std::exception& err) {                           \
            ku::Fail(err.what());                                       \
        }                                                               \
    }                                                                   \
    ret_type cls::name##Impl(arg1_type arg1_name, arg2_type arg2_name)

#endif // JS_COMMON_H
