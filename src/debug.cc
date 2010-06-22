
// (c) 2009-2010 by Anton Korenyushkin

#include "debug.h"

#include <boost/lexical_cast.hpp>
#include <boost/utility.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/date_time/local_time/local_time.hpp>

#include <execinfo.h>


using namespace std;
using namespace ak;
using boost::lexical_cast;
using boost::noncopyable;
using boost::scoped_ptr;
using boost::posix_time::ptime;
using boost::local_time::posix_time_zone;
using boost::local_time::time_zone_ptr;
using boost::local_time::local_sec_clock;


namespace
{
    const int MAX_BACKTRACE_SIZE = 4 * 1024;
}


string ak::log_prefix = "";


void ak::Log(const string& message)
{
    // Time zone management on linux is so tricky
    // that I preferred to handcode my time zone.
    static const time_zone_ptr zone_ptr(
        new posix_time_zone("MSK+3MSD+01,M3.5.0/02:00,M10.5.0/02:00"));
    cerr << local_sec_clock::local_time(zone_ptr).local_time()
         << ' '
         << log_prefix
         << message
         << '\n';
    cerr.flush();
}


string ak::Backtrace()
{
#ifdef BACKTRACE
    void* buffer[MAX_BACKTRACE_SIZE];
    size_t size = backtrace(buffer, MAX_BACKTRACE_SIZE);
    char** symbols_ptr = backtrace_symbols(buffer, size);
    if (!symbols_ptr)
        return "Failed to get backtrace!!!\n";
    string result;
    for (size_t i = 0; i < size; ++i) {
        result += symbols_ptr[i];
        result += '\n';
    }
    free(symbols_ptr);
    return result;
#else
    return "";
#endif
}


void ak::Fail(const string& message)
{
    Log("Fail: " + message
#ifdef BACKTRACE
        + '\n' + Backtrace()
#endif
        );
    exit(1);
}


void ak::FailOnAssertion(const string& file,
                         int line,
                         const string& pretty_function,
                         const string& assertion,
                         const string& message)
{
    ostringstream oss;
    oss << file << ':' << line << ": " << pretty_function << ": "
        << "Assertion `" << assertion << "' failed";
    if (message.empty())
        oss << '.';
    else
        oss << ": " << message << '.';
    Fail(oss.str());
}
