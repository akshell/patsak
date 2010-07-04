
// (c) 2009-2010 by Anton Korenyushkin

#ifndef JS_H
#define JS_H

#include "common.h"

#include <boost/scoped_ptr.hpp>


namespace ak
{
    class DB;


    class Program {
    public:
        Program(const std::string& code_path,
                const std::string& media_path,
                const std::string& git_path_prefix,
                const std::string& git_path_suffix,
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
