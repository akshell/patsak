
// (c) 2009-2010 by Anton Korenyushkin

#include "js-common.h"

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

#include <signal.h>


using namespace ak;
using namespace v8;
using namespace std;
using boost::lexical_cast;


////////////////////////////////////////////////////////////////////////////////
// Utils
////////////////////////////////////////////////////////////////////////////////

Access* ak::access_ptr;
Persistent<Object> ak::js_error_classes;


void ak::ThrowError(const ak::Error& err)
{
    Handle<v8::Value> message(String::New(err.what()));
    ThrowException(
        Function::Cast(*js_error_classes->Get(Integer::New(err.GetTag())))
        ->NewInstance(1, &message));
}


string ak::Stringify(Handle<v8::Value> value)
{
    String::Utf8Value utf8_value(value);
    return string(*utf8_value, utf8_value.length());
}


void ak::CheckArgsLength(const Arguments& args, int length)
{
    if (args.Length() < length)
        throw Error(Error::USAGE,
                    ("At least " +
                     lexical_cast<string>(length) +
                     " arguments required"));
}


Handle<Array> ak::GetArray(Handle<v8::Value> value)
{
    if (!value->IsArray())
        throw Error(Error::TYPE, "Array required");
    return Handle<Array>::Cast(value);
}


Handle<v8::Value> ak::Get(Handle<Object> object, const string& name)
{
    return object->Get(String::New(name.c_str()));
}


void ak::SetFunction(Handle<ObjectTemplate> object_template,
                     const string& name,
                     InvocationCallback callback)
{
    AK_ASSERT(!name.empty());
    Set(object_template,
        name.c_str(),
        FunctionTemplate::New(callback),
        name[0] == '_' ? DontEnum : None);
}


void ak::SetFunction(Handle<Object> object,
                     const string& name,
                     InvocationCallback callback)
{
    AK_ASSERT(!name.empty());
    Set(object,
        name,
        FunctionTemplate::New(callback)->GetFunction(),
        name[0] == '_' ? DontEnum : None);
}

////////////////////////////////////////////////////////////////////////////////
// ExecutionGuard, CallbackGuard, and TimedOut
////////////////////////////////////////////////////////////////////////////////

namespace
{
    bool handler_set = false;
    bool timed_out   = false;
    bool in_callback = false;


    void HandleAlarm(int /*signal*/)
    {
        timed_out = true;
        if (!in_callback)
            V8::TerminateExecution();
    }
}


ExecutionGuard::ExecutionGuard()
{
    if (!handler_set) {
        struct sigaction action;
        action.sa_handler = HandleAlarm;
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0;
        sigaction(SIGALRM, &action, 0);
        handler_set = true;
    }
    timed_out = false;
    in_callback = false;
    alarm(10);
}


ExecutionGuard::~ExecutionGuard()
{
    alarm(0);
}


CallbackGuard::CallbackGuard()
{
    in_callback = true;
}


CallbackGuard::~CallbackGuard() {
    in_callback = false;
    if (timed_out)
        v8::V8::TerminateExecution();
}


bool ak::TimedOut()
{
    return timed_out;
}

////////////////////////////////////////////////////////////////////////////////
// JSClassBase
////////////////////////////////////////////////////////////////////////////////

JSClassBase::JSClassBase(const string& name,
                         JSClassBase* parent_ptr,
                         v8::InvocationCallback constructor)
    : name_(name)
    , function_template_(FunctionTemplate::New(constructor))
{
    GetInstancePtrs().push_back(this);
    function_template_->SetClassName(String::New(name.c_str()));
    GetObjectTemplate()->SetInternalFieldCount(1);
    cast_js_classes_.push_back(function_template_);
    if (parent_ptr)
        parent_ptr->cast_js_classes_.push_back(function_template_);
}


JSClassBase::~JSClassBase()
{
    type_switch_.Dispose();
    function_.Dispose();
    function_template_.Dispose(); // NB: used to segfault in cov mode
}


string JSClassBase::GetName() const
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
        AK_ASSERT(type_switch_.IsEmpty());
        AK_ASSERT(!cast_js_classes_.empty());
        type_switch_ = Persistent<TypeSwitch>::New(
            TypeSwitch::New(cast_js_classes_.size(), &cast_js_classes_[0]));
        cast_js_classes_.clear();
        function_ = Persistent<Function>::New(
            function_template_->GetFunction());
    }
    return function_;
}


void* JSClassBase::Cast(Handle<v8::Value> value)
{
    GetFunction();
    if (!type_switch_->match(value))
        return 0;
    Handle<v8::Value> internal_field(value->ToObject()->GetInternalField(0));
    AK_ASSERT(internal_field->IsExternal());
    Handle<External> external = Handle<External>::Cast(internal_field);
    return external->Value();
}


void JSClassBase::InitConstructors(Handle<Object> holder)
{
    BOOST_FOREACH(JSClassBase* class_ptr, GetInstancePtrs()) {
        const string& name(class_ptr->GetName());
        AK_ASSERT(name.size());
        Set(holder, name, class_ptr->GetFunction());
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
