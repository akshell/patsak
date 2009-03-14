
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


    class EvalResult {
    public:
        virtual ~EvalResult() {}
        virtual std::string GetStatus() const = 0;
        virtual size_t GetSize() const = 0;
        virtual const char* GetData() const = 0;
    };
    

    /// JavaScript program abstraction
    class Program {
    public:
        Program(const std::string& file_path, DB& db, std::ostream& out);
        ~Program();
        std::auto_ptr<EvalResult> Eval(const std::string& expr_str);
        
    private:
        class Impl;
        boost::scoped_ptr<Impl> pimpl_;
    };
}

#endif // JS_H
