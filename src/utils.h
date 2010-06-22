
// (c) 2008-2010 by Anton Korenyushkin

#ifndef UTILS_H
#define UTILS_H

#include "common.h"

#include <boost/function.hpp>
#include <boost/variant.hpp>

#include <ostream>


namespace ak
{
    // Functor which passes count times, then calls a given function
    class OmitInvoker {
    public:
        typedef boost::function<void ()> Func;

        OmitInvoker(Func func, int count = 1)
            : func_(func), count_(count) {}

        void operator()() {
            if (count_ > 0)
                --count_;
            else
                func_();
        }

    private:
        Func func_;
        int count_;
    };


    // Print symbol to a stream
    class SepPrinter {
    public:
        SepPrinter(std::ostream& os, const std::string& sep = ", ")
            : os_(os), sep_(sep) {}

        void operator()() const { os_ << sep_; }

    private:
        std::ostream& os_;
        std::string sep_;
    };


    inline std::string Quoted(const std::string& str) {
        return '"' + str + '"';
    }


    inline Types GetValuesTypes(const Values& values)
    {
        Types result;
        result.reserve(values.size());
        for (Values::const_iterator itr = values.begin();
             itr != values.end();
             ++itr)
            result.push_back(itr->GetType());
        return result;
    }


    // Wrapper holding data after setting
    // NB in most cases in parser data are retrieved only once, so
    // they could be freed immediately.
    template <typename T>
    class Wrapper {
    public:
        Wrapper(const T& data) : data_(data) {} // implicit

        Wrapper() {}

        Wrapper& operator=(const T& data) {
            data_ = data;
            return *this;
        }

        operator const T&() const { return Get();                 }
        const T& Get()      const { return boost::get<T>(data_);  }
        T& Get()                  { return boost::get<T>(data_);  }
        const T* GetPtr()   const { return boost::get<T>(&data_); }
        T* GetPtr()               { return boost::get<T>(&data_); }

    private:
        struct Null {};

        boost::variant<Null, T> data_;
    };
}

#endif // UTILS_H
