
// (c) 2009-2010 by Anton Korenyushkin

/// \file debug.cc
/// Debugging stuff definitions

#include "debug.h"

#include <boost/lexical_cast.hpp>
#include <boost/utility.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/date_time/local_time/local_time.hpp>

#include <execinfo.h>


using namespace std;
using namespace ku;
using boost::lexical_cast;
using boost::noncopyable;
using boost::scoped_ptr;
using boost::posix_time::ptime;
using boost::local_time::posix_time_zone;
using boost::local_time::time_zone_ptr;
using boost::local_time::local_sec_clock;

////////////////////////////////////////////////////////////////////////////////
// Constants
////////////////////////////////////////////////////////////////////////////////

namespace
{
    const int MAX_BACKTRACE_SIZE = 4 * 1024;
}

////////////////////////////////////////////////////////////////////////////////
// TheLogger
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class TheLogger : public noncopyable {
    public:
        static TheLogger& GetInstance();
        void OpenFile(const string& file_name);
        void Log(const string& message) const;
        void Close();

    private:
        scoped_ptr<ofstream> ofs_ptr_;

        ostream& GetOstream() const;
        
        TheLogger() {}
        ~TheLogger();
    };


    // Time zone management on linux is so tricky
    // that I preferred to handcode my time zone.
    ptime GetMoscowTime() {
        static const time_zone_ptr zone_ptr(
            new posix_time_zone("MSK+3MSD+01,M3.5.0/02:00,M10.5.0/02:00"));
        return local_sec_clock::local_time(zone_ptr).local_time();
    }
}


TheLogger& TheLogger::GetInstance()
{
    static TheLogger instance;
    return instance;
}


TheLogger::~TheLogger()
{
    Close();
}

        
void TheLogger::OpenFile(const string& file_name)
{
    ofs_ptr_.reset(new ofstream(file_name.c_str(), ios_base::app));
    if (!ofs_ptr_->is_open()) {
        cerr << "Failed to open log file " << file_name << '\n';
        abort();
    }
}


void TheLogger::Log(const string& message) const
{
    ostream& os(ofs_ptr_ ? *ofs_ptr_ : cerr);
    os << GetMoscowTime()
       << ' '
       << message
       << '\n';
    os.flush();
}


void TheLogger::Close()
{
    if (ofs_ptr_) {
        ofs_ptr_->close();
        ofs_ptr_.reset();
    } else {
        cerr.flush();
    }
}

////////////////////////////////////////////////////////////////////////////////
// Free functions
////////////////////////////////////////////////////////////////////////////////

string ku::Backtrace()
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


void ku::Log(const string& message)
{
    TheLogger::GetInstance().Log(message);
}


void ku::OpenLogFile(const string& file_name)
{
    TheLogger::GetInstance().OpenFile(file_name);
}


void ku::Fail(const string& message)
{
    Log("Fail: " + message
#ifdef BACKTRACE
        + '\n' + Backtrace()
#endif
        );
    TheLogger::GetInstance().Close();
    exit(1);
}


void ku::FailOnAssertion(const char* cond,
                         const char* file,
                         int line,
                         const char* pretty_function)
{
    Fail(string(file) + ':' +
         lexical_cast<string>(line) + ": " +
         pretty_function + ": " +
         "Assertion `" + cond + "' failed.");
}
