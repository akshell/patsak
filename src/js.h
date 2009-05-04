
// (c) 2009 by Anton Korenyushkin

/// \file js.h
/// JavaScript interpreter interface

#ifndef JS_H
#define JS_H

#include "common.h"

#include <boost/scoped_ptr.hpp>

#include <string>
#include <iosfwd>
#include <memory>


namespace ku
{
    class DB;


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
        enum Status {
            OK,
            NO_SUCH_APP,
            INVALID_APP_NAME,
            SELF_CALL,
            TIMED_OUT
        };
        
        virtual Status Process(const std::string& app_name,
                               const Chars* data_ptr,
                               const Strings& file_pathes,
                               const std::string& request,
                               Chars& result) = 0;

        virtual bool Exists(const std::string& app_name) = 0;
    };
    

    /// JavaScript program abstraction
    class Program {
    public:
        Program(const std::string& code_dir,
                const std::string& include_dir,
                const std::string& media_dir,
                DB& db,
                AppAccessor& app_evaluator);
        
        ~Program();
        
        std::auto_ptr<Response> Process(
            const Chars& request,
            const Strings& file_pathes = Strings(),
            std::auto_ptr<Chars> data_ptr = std::auto_ptr<Chars>());
        
    private:
        class Impl;
        boost::scoped_ptr<Impl> pimpl_;
    };
}

#endif // JS_H
