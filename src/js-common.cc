
// (c) 2009 by Anton Korenyushkin

/// \file js-common.cc
/// General JavaScript helpers impl

#include "js-common.h"

#include <boost/foreach.hpp>


using namespace ku;
using namespace v8;
using namespace std;


////////////////////////////////////////////////////////////////////////////////
// JSClassBase
////////////////////////////////////////////////////////////////////////////////

JSClassBase::JSClassBase(const std::string& name)
    : name_(name)
    , function_template_(FunctionTemplate::New())
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
    function_template_.Dispose();
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


void* JSClassBase::Cast(Handle<Value> value)
{
    GetFunction();
    if (!type_switch_->match(value))
        return 0;
    Handle<Value> internal_field(value->ToObject()->GetInternalField(0));
    KU_ASSERT(internal_field->IsExternal());
    Handle<External> external = Handle<External>::Cast(internal_field);
    return external->Value();
}


void JSClassBase::InitConstructors(Handle<Object> holder)
{
    BOOST_FOREACH(JSClassBase* class_ptr, GetInstancePtrs())
        holder->Set(String::NewSymbol(class_ptr->name_.c_str()),
                    class_ptr->GetFunction());
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
