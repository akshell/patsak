
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


    class DBMediatorBg {
    public:
        DECLARE_JS_CLASS(DBMediatorBg);
        
        DBMediatorBg();

        void Init(v8::Handle<v8::Object> object) const;

    private:
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, QueryCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, DropRelVarsCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, UniqueCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ForeignCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, CheckCb,
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


    class DBBg {
    public:
        DECLARE_JS_CLASS(DBBg);
        
        DBBg();

    private:
        DECLARE_JS_CALLBACK2(v8::Handle<v8::Value>, GetRelVarCb,
                             v8::Local<v8::String>,
                             const v8::AccessorInfo&) const;
        
        static v8::Handle<v8::Value> SetRelVarCb(v8::Local<v8::String> property,
                                                 v8::Local<v8::Value> value,
                                                 const v8::AccessorInfo& info);
        
        DECLARE_JS_CALLBACK2(v8::Handle<v8::Boolean>, HasRelVarCb,
                             v8::Local<v8::String>,
                             const v8::AccessorInfo&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Array>, EnumRelVarsCb,
                             const v8::AccessorInfo&) const;
    };
}

#endif // JS_DB_H
