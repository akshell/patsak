
// (c) 2009-2010 by Anton Korenyushkin

/// \file js.h
/// JavaScript interpreter interface

#ifndef JS_H
#define JS_H

#include "common.h"


namespace ku
{
    class DB;
    class Access;


    /// Program response interface
    class Response {
    public:
        virtual ~Response() {}
        virtual std::string GetStatus() const = 0;
        virtual size_t GetSize() const = 0;
        virtual const char* GetData() const = 0;
    };


    /// Other application accessor interface
    class AppAccessor {
    public:
        virtual Chars operator()(const std::string& app_name,
                                 const std::string& request,
                                 const Strings& file_pathes,
                                 const Chars* data_ptr,
                                 const Access& access) = 0;
    };


    struct Place {
        const std::string app_name;
        const std::string owner_name;
        const std::string spot_name;

        Place(const std::string& app_name,
              const std::string& owner_name,
              const std::string& spot_name)
            : app_name(app_name)
            , owner_name(owner_name)
            , spot_name(spot_name) {
            KU_ASSERT((owner_name.empty() && spot_name.empty()) ||
                      (!owner_name.empty() && !spot_name.empty()));
        }
    };
    

    /// JavaScript program abstraction
    class Program {
    public:
        Program(const Place& place,
                const std::string& code_dir,
                const std::string& include_dir,
                const std::string& media_dir,
                DB& db,
                AppAccessor& app_accessor);
        
        ~Program();
        
        std::auto_ptr<Response> Process(
            const std::string& user,
            const Chars& request,
            const Strings& file_pathes = Strings(),
            std::auto_ptr<Chars> data_ptr = std::auto_ptr<Chars>(),
            const std::string& issuer = "");

        std::auto_ptr<Response> Eval(const std::string& user,
                                     const Chars& expr);

        bool IsDead() const;
        
    private:
        class Impl;
        boost::scoped_ptr<Impl> pimpl_;
    };
}

#endif // JS_H
