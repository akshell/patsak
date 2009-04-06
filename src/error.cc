
// (c) 2009 by Anton Korenyushkin

/// \file error.cc
/// Error handling stuff definitions

#include "error.h"

#include <boost/lexical_cast.hpp>
#include <boost/utility.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/date_time/time_clock.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <execinfo.h>
#include <stdlib.h>
#include <fstream>


using namespace std;
using namespace ku;
using boost::lexical_cast;
using boost::noncopyable;
using boost::scoped_ptr;
using boost::date_time::second_clock;
using boost::posix_time::ptime;


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
    os << second_clock<ptime>::local_time()
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

#ifndef NDEBUG
void FailOnAssertion__(const char* cond,
                       const char* file,
                       int line,
                       const char* pretty_function)
{
    Fail(string(file) + ':' +
         lexical_cast<string>(line) + ": " +
         pretty_function + ": " +
         "Assertion `" + cond + "' failed."
#ifdef BACKTRACE
         + '\n' +Backtrace()
#endif
        );
}
#endif


void ku::Log(const string& message)
{
    TheLogger::GetInstance().Log(message);
}


void ku::OpenLogFile(const string& file_name)
{
    TheLogger::GetInstance().OpenFile(file_name);
}


#ifdef BACKTRACE
string ku::Backtrace()
{
    void* buffer[512];
    size_t size = backtrace(buffer, 512);
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
}
#endif


void ku::Fail(const string& message)
{
    Log("Fail: " + message);
    TheLogger::GetInstance().Close();
    exit(1);
}
