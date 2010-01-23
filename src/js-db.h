
// (c) 2009-2010 by Anton Korenyushkin

/// \file js-db.h
/// JavaScript database stuff interfaces

#ifndef JS_DB_H
#define JS_DB_H

#include "js-common.h"


namespace ku
{
    class Access;
    extern Access* access_ptr;


    class DBBg {
    public:
        DECLARE_JS_CLASS(DBBg);
        void Init(v8::Handle<v8::Object> object) const;

    private:
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, QueryCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, CountCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, CreateCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, DropCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ListCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, GetHeaderCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, GetIntegerCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, GetSerialCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, GetDefaultCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, GetUniqueCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, GetForeignCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, InsertCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, DelCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, UpdateCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, DescribeAppCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, GetAdminedAppsCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, GetDevelopedAppsCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, GetAppsByLabelCb,
                             const v8::Arguments&) const;
    };
}

#endif // JS_DB_H
