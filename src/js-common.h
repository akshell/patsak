
// (c) 2009 by Anton Korenyushkin

/// \file js-common.h
/// General JavaScript helpers

#ifndef JS_COMMON_H
#define JS_COMMON_H

#include "common.h"

#include <v8.h>
#include <boost/utility.hpp>


////////////////////////////////////////////////////////////////////////////////
// Stuff
////////////////////////////////////////////////////////////////////////////////

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
    void ThrowError(const ku::Error& err);
    std::string Stringify(v8::Handle<v8::Value> value);
    void CheckArgsLength(const v8::Arguments& args, int length);

    int32_t GetArrayLikeLength(v8::Handle<v8::Value> value);

    v8::Handle<v8::Value> GetArrayLikeItem(v8::Handle<v8::Value> value,
                                           int32_t index);

    void SetFunction(v8::Handle<v8::Template> template_,
                     const std::string& name,
                     v8::InvocationCallback callback);

    
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

////////////////////////////////////////////////////////////////////////////////
// JSClass
////////////////////////////////////////////////////////////////////////////////

namespace ku
{
    class JSClassBase : boost::noncopyable {
    public:
        std::string GetName() const;
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
    
    
    template <typename T>
    T& GetBg(v8::Handle<v8::Value> holder)
    {
        JSClass<T>& js_class(T::GetJSClass());
        T* bg_ptr = js_class.Cast(holder);
        if (!bg_ptr)
            throw ku::Error(ku::Error::TYPE,
                            js_class.GetName() + " object was expected");
        return *bg_ptr;
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
        try {                                                           \
            return ku::GetBg<cls>(arg.Holder()).name##Impl(arg);        \
        } catch (const ku::Error& err) {                                \
            ku::ThrowError(err);                                        \
            return ret_type();                                          \
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
        try {                                                           \
            return ku::GetBg<cls>(arg2.Holder()).name##Impl(arg1, arg2);\
        } catch (const ku::Error& err) {                                \
            ku::ThrowError(err);                                        \
            return ret_type();                                          \
        } catch (const std::exception& err) {                           \
            ku::Fail(err.what());                                       \
        }                                                               \
    }                                                                   \
    ret_type cls::name##Impl(arg1_type arg1_name, arg2_type arg2_name)

#endif // JS_COMMON_H
