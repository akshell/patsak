
// (c) 2009-2010 by Anton Korenyushkin

#ifndef JS_H
#define JS_H

#include "common.h"

#include <boost/scoped_ptr.hpp>


namespace ak
{
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
            AK_ASSERT((owner_name.empty() && spot_name.empty()) ||
                      (!owner_name.empty() && !spot_name.empty()));
        }
    };


    class DB;


    class Program {
    public:
        Program(const Place& place,
                const std::string& app_code_path,
                const std::string& release_code_path,
                const std::string& app_media_path,
                const std::string& release_media_path,
                DB& db);

        ~Program();

        void Process(int sock_fd);
        void Eval(const Chars& expr, int out_fd);
        bool IsDead() const;

    private:
        class Impl;
        boost::scoped_ptr<Impl> pimpl_;
    };
}

#endif // JS_H
