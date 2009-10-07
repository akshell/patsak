
// (c) 2009 by Anton Korenyushkin

/// \file js-common.cc
/// General JavaScript helpers impl

#include "js-common.h"

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>


using namespace ku;
using namespace v8;
using namespace std;
using boost::lexical_cast;


////////////////////////////////////////////////////////////////////////////////
// Stuff
////////////////////////////////////////////////////////////////////////////////

namespace
{
    Handle<Object> GetErrors()
    {
        return (Context::GetCurrent()->Global()
                ->Get(String::NewSymbol("ak"))->ToObject()
                ->Get(String::NewSymbol("_errors"))->ToObject());
    }
}


void ku::ThrowError(const ku::Error& err) {
    static Persistent<Object> errors(Persistent<Object>::New(GetErrors()));
    Handle<v8::Value> message(String::New(err.what()));
    ThrowException(
        Function::Cast(*errors->Get(Integer::New(err.GetTag())))
        ->NewInstance(1, &message));
}


string ku::Stringify(Handle<v8::Value> value)
{
    String::Utf8Value utf8_value(value);
    return string(*utf8_value, utf8_value.length());
}


void ku::CheckArgsLength(const Arguments& args, int length) {
    if (args.Length() < length)
        throw Error(Error::USAGE,
                    ("At least " +
                     lexical_cast<string>(length) +
                     " arguments required"));
}


int32_t ku::GetArrayLikeLength(Handle<v8::Value> value)
{
    if (!value->IsObject())
        return -1;
    Handle<Object> object(value->ToObject());
    Handle<String> length_string(String::NewSymbol("length"));
    if (!object->Has(length_string))
        return -1;
    Handle<v8::Value> length_value(object->Get(length_string));
    if (!length_value->IsInt32())
        return -1;
    int32_t result = length_value->ToInt32()->Value();
    if (result < 0)
        return -1;
    return result;
}


Handle<v8::Value> ku::GetArrayLikeItem(Handle<v8::Value> value, int32_t index)
{
    Handle<Object> object(value->ToObject());
    KU_ASSERT(!object.IsEmpty());
    Handle<v8::Value> index_value(Integer::New(index));
    if (!object->Has(index_value->ToString()))
        throw Error(Error::TYPE, "Bad array like object");
    Handle<v8::Value> result(object->Get(index_value));
    return result;
}


void ku::SetFunction(Handle<Template> template_,
                     const string& name,
                     InvocationCallback callback)
{
    template_->Set(String::NewSymbol(name.c_str()),
                   FunctionTemplate::New(callback),
                   DontEnum);
}

////////////////////////////////////////////////////////////////////////////////
// JSClassBase
////////////////////////////////////////////////////////////////////////////////

JSClassBase::JSClassBase(const std::string& name,
                         v8::InvocationCallback constructor)
    : name_(name)
    , function_template_(FunctionTemplate::New(constructor))
{
    GetInstancePtrs().push_back(this);
    function_template_->SetClassName(String::New(name.c_str()));
    GetObjectTemplate()->SetInternalFieldCount(1);
    cast_js_classes_.push_back(function_template_);
}


JSClassBase::~JSClassBase()
{
    type_switch_.Dispose();
    function_.Dispose();
//     function_template_.Dispose(); // causes segfault in cov mode
}


std::string JSClassBase::GetName() const
{
    return name_;
}


Handle<ObjectTemplate> JSClassBase::GetObjectTemplate() const
{
    return function_template_->InstanceTemplate();
}


Handle<Function> JSClassBase::GetFunction()
{
    if (function_.IsEmpty()) {
        KU_ASSERT(type_switch_.IsEmpty());
        KU_ASSERT(!cast_js_classes_.empty());
        type_switch_ = Persistent<TypeSwitch>::New(
            TypeSwitch::New(cast_js_classes_.size(), &cast_js_classes_[0]));
        cast_js_classes_.clear();
        function_ = Persistent<Function>::New(
            function_template_->GetFunction());
    }
    return function_;
}


void JSClassBase::AddSubClass(const JSClassBase& subclass)
{
    KU_ASSERT(!cast_js_classes_.empty());
    cast_js_classes_.push_back(subclass.function_template_);
}


void* JSClassBase::Cast(Handle<v8::Value> value)
{
    GetFunction();
    while (!type_switch_->match(value)) {
        if (!value->IsObject())
            return 0;
        value = value->ToObject()->GetPrototype();
    }
    Handle<v8::Value> internal_field(value->ToObject()->GetInternalField(0));
    KU_ASSERT(internal_field->IsExternal());
    Handle<External> external = Handle<External>::Cast(internal_field);
    return external->Value();
}


void JSClassBase::InitConstructors(Handle<Object> holder)
{
    BOOST_FOREACH(JSClassBase* class_ptr, GetInstancePtrs()) {
        const string& name(class_ptr->GetName());
        KU_ASSERT(name.size());
        holder->Set(String::New(name.c_str()),
                    class_ptr->GetFunction(),
                    (name[0] == '_' ? DontEnum : None));
    }
}


Handle<ObjectTemplate> JSClassBase::GetProtoTemplate() const
{
    return function_template_->PrototypeTemplate();
}


vector<JSClassBase*>& JSClassBase::GetInstancePtrs()
{
    static vector<JSClassBase*> instance_ptrs;
    return instance_ptrs;
}
