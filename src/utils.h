
// (c) 2008-2010 by Anton Korenyushkin

#ifndef UTILS_H
#define UTILS_H

#include "common.h"

#include <boost/function.hpp>

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
}

#endif // UTILS_H
