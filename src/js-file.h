
// (c) 2009 by Anton Korenyushkin

/// \file js-data.h
/// JavaScript binary data handler interface

#ifndef JS_DATA_H
#define JS_DATA_H

#include "js-common.h"

#include <boost/shared_ptr.hpp>


namespace ku
{
    /// Data background
    class DataBg {
    public:
        DECLARE_JS_CLASS(DataBg);

        DataBg(std::auto_ptr<std::vector<char> > data_ptr);
        ~DataBg();

    private:
        boost::shared_ptr<std::vector<char> > data_ptr_;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ToStringCb,
                             const v8::Arguments&) const;
    };
}

#endif // JS_DATA_H
