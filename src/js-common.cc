
// (c) 2009-2010 by Anton Korenyushkin

#include "js-common.h"

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

#include <signal.h>


using namespace ku;
using namespace v8;
using namespace std;
using boost::lexical_cast;


////////////////////////////////////////////////////////////////////////////////
// Stuff
////////////////////////////////////////////////////////////////////////////////

void ku::ThrowError(const ku::Error& err) {
    static Persistent<Object> errors(
        Persistent<Object>::New(
            Get(Get(Context::GetCurrent()->Global(), "ak")->ToObject(),
                "_errors")->ToObject()));
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


size_t ku::GetArrayLikeLength(Handle<v8::Value> value)
{
    if (!value->IsObject())
        throw Error(Error::TYPE, "Array-like required (non-object provided)");
    Handle<Object> object(value->ToObject());
    Handle<String> length_string(String::NewSymbol("length"));
    if (!object->Has(length_string))
        throw Error(Error::TYPE, "Array-like required (length not found)");
    Handle<v8::Value> length_value(object->Get(length_string));
    if (!length_value->IsInt32())
        throw Error(Error::TYPE, "Array-like required (length is not integer)");
    int32_t result = length_value->ToInt32()->Value();
    if (result < 0)
        throw Error(Error::TYPE, "Array-like required (length is negative)");
    return result;
}


Handle<v8::Value> ku::GetArrayLikeItem(Handle<v8::Value> value, size_t index)
{
    return value->ToObject()->Get(Integer::New(index));
}


Handle<v8::Value> ku::Get(Handle<Object> object, const string& name)
{
    return object->Get(String::New(name.c_str()));
}


void ku::SetFunction(Handle<Template> template_,
                     const string& name,
                     InvocationCallback callback)
{
    Set(template_,
        ('_' + name).c_str(),
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


void* JSClassBase::Cast(Handle<v8::Value> value)
{
    GetFunction();
    if (!type_switch_->match(value))
        return 0;
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
        Set(holder, name,
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

////////////////////////////////////////////////////////////////////////////////
// Watcher
////////////////////////////////////////////////////////////////////////////////

bool Watcher::initialized_ = false;
bool Watcher::timed_out_   = false;
bool Watcher::in_callback_ = false;


void Watcher::HandleAlarm(int /*signal*/)
{
    Watcher::timed_out_ = true;
    if (!Watcher::in_callback_)
        V8::TerminateExecution();
}


Watcher::ExecutionGuard::ExecutionGuard()
{
    if (!Watcher::initialized_) {
        struct sigaction action;
        action.sa_handler = Watcher::HandleAlarm;
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0;
        sigaction(SIGALRM, &action, 0);
        Watcher::initialized_ = true;
    }
    Watcher::timed_out_ = false;
    Watcher::in_callback_ = false;
    alarm(10);
}


Watcher::ExecutionGuard::~ExecutionGuard()
{
    alarm(0);
}
