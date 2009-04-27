
// (c) 2009 by Anton Korenyushkin

/// \file js-data.cc
/// JavaScript binary data handler impl

#include "js-file.h"


using namespace ku;
using namespace v8;
using namespace std;
using boost::shared_ptr;


////////////////////////////////////////////////////////////////////////////////
// DataStringResource
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class DataStringResource : public String::ExternalAsciiStringResource {
    public:
        DataStringResource(shared_ptr<vector<char> > data_ptr);
        virtual ~DataStringResource();
        virtual const char* data() const;
        virtual size_t length() const;
        
    private:
        shared_ptr<vector<char> > data_ptr_;
    };
}


DataStringResource::DataStringResource(shared_ptr<vector<char> > data_ptr)
    : data_ptr_(data_ptr)
{
    KU_ASSERT(data_ptr_);
}


DataStringResource::~DataStringResource()
{
}


const char* DataStringResource::data() const
{
    return &(data_ptr_->front());
}


size_t DataStringResource::length() const
{
    return data_ptr_->size();
}

////////////////////////////////////////////////////////////////////////////////
// DataBg
////////////////////////////////////////////////////////////////////////////////

DEFINE_JS_CLASS(DataBg, "Data", /*object_template*/, proto_template)
{
    proto_template->Set(String::NewSymbol("toString"),
                        FunctionTemplate::New(&ToStringCb),
                        DontEnum);
}


DataBg::DataBg(std::auto_ptr<std::vector<char> > data_ptr)
    : data_ptr_(data_ptr.release())
{
    KU_ASSERT(data_ptr_);
    V8::AdjustAmountOfExternalAllocatedMemory(data_ptr_->size());
}


DataBg::~DataBg()
{
    V8::AdjustAmountOfExternalAllocatedMemory(-data_ptr_->size());
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DataBg, ToStringCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 0);
    return String::NewExternal(new DataStringResource(data_ptr_));
}
