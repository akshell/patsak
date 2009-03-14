
// (c) 2009 by Anton Korenyushkin

/// \file js-db.h
/// JavaScript database stuff interfaces

#ifndef JS_DB_H
#define JS_DB_H

#include "js-common.h"
#include "common.h"

#include <boost/utility.hpp>


namespace ku
{
    class Access;
    

    /// Database access holder
    class AccessHolder : boost::noncopyable {
    public:
        class Scope {
        public:
            Scope(AccessHolder& access_holder, Access& access);
            ~Scope();

        private:
            AccessHolder& access_holder_;
        };

        AccessHolder();
        Access& operator*() const;
        Access* operator->() const;

    private:
        Access* access_ptr_;
    };


    /// type background
    class TypeCatalogBg {
    public:
        DECLARE_JS_CLASS(TypeCatalogBg);

    private:
        DECLARE_JS_CALLBACK2(v8::Handle<v8::Value>, GetTypeCb,
                             v8::Local<v8::String>,
                             const v8::AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(v8::Handle<v8::Boolean>, HasTypeCb,
                             v8::Local<v8::String>,
                             const v8::AccessorInfo&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Array>, EnumTypesCb,
                             const v8::AccessorInfo&) const;
    };


    /// db background
    class DbBg {
    public:
        DECLARE_JS_CLASS(DbBg);
        
        DbBg(const AccessHolder& access_holder);

    private:
        const AccessHolder& access_holder_;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, QueryCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, CreateRelCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, DropRelsCb,
                             const v8::Arguments&) const;
    };


    /// rel background
    class RelCatalogBg {
    public:
        DECLARE_JS_CLASS(RelCatalogBg);
        
        RelCatalogBg(const AccessHolder& access_holder);

    private:
        const AccessHolder& access_holder_;

        DECLARE_JS_CALLBACK2(v8::Handle<v8::Value>, GetRelCb,
                             v8::Local<v8::String>,
                             const v8::AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(v8::Handle<v8::Boolean>, HasRelCb,
                             v8::Local<v8::String>,
                             const v8::AccessorInfo&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Array>, EnumRelsCb,
                             const v8::AccessorInfo&) const;
    };


    /// constr background
    class ConstrCatalogBg {
    public:
        DECLARE_JS_CLASS(ConstrCatalogBg);
        
    private:
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, PrimaryKeyCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, UniqueCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ForeignKeyCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, CheckCb,
                             const v8::Arguments&) const;
    };


    /// Initialize ku members - constructors of all database related objects
    void InitDBConstructors(v8::Handle<v8::Object> ku);
}

#endif // JS_DB_H
