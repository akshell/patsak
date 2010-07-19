// (c) 2009-2010 by Anton Korenyushkin

#include "debug.h"

#include <iostream>
#include <time.h>
#include <stdlib.h>
#include <execinfo.h>


using namespace std;
using namespace ak;


namespace
{
    const size_t TIME_SIZE = 21;
    const size_t MAX_BACKTRACE_SIZE = 8192;


    void PrintPrefix()
    {
        time_t t = time(0);
        const struct tm* tm_ptr = localtime(&t);
        char buf[TIME_SIZE];
        size_t size = strftime(buf, TIME_SIZE, "%F %T ", tm_ptr);
        cerr.write(buf, size);
    }


    void PrintBacktrace()
    {
        void* buffer[MAX_BACKTRACE_SIZE];
        size_t size = backtrace(buffer, MAX_BACKTRACE_SIZE);
        backtrace_symbols_fd(buffer, size, STDERR_FILENO);
    }
}


void ak::Fail(const string& message)
{
    PrintPrefix();
    cerr << "Fail: " << message << '\n';
    PrintBacktrace();
    exit(1);
}


void ak::FailOnAssertion(const string& file,
                         int line,
                         const string& pretty_function,
                         const string& assertion,
                         const string& message)
{
    PrintPrefix();
    cerr << file << ':' << line << ": " << pretty_function << ": "
         << "Assertion `" << assertion << "' failed";
    if (!message.empty())
        cerr << ": " << message;
    cerr << ".\n";
    PrintBacktrace();
    exit(1);
}
