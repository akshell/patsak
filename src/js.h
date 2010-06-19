
// (c) 2009-2010 by Anton Korenyushkin

#ifndef JS_H
#define JS_H

#include "common.h"

#include <boost/scoped_ptr.hpp>


namespace ku
{
    class DB;
    class Access;


    class Response {
    public:
        virtual ~Response() {}
        virtual std::string GetStatus() const = 0;
        virtual size_t GetSize() const = 0;
        virtual const char* GetData() const = 0;
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
    

    class Program {
    public:
        Program(const Place& place,
                const std::string& app_code_path,
                const std::string& release_code_path,
                const std::string& app_media_path,
                const std::string& release_media_path,
                DB& db);
        
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
