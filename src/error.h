
// (c) 2008-2009 by Anton Korenyushkin

/// \file error.h
/// Error handling stuff

#ifndef ERROR_H
#define ERROR_H

#include <string>
#include <stdexcept>


#ifdef NDEBUG

#define KU_ASSERT(cond)

#else // NDEBUG

/// Internal assertion fail function
void FailOnAssertion__(const char* cond,
                       const char* file,
                       int line,
                       const char* pretty_function) __attribute__((noreturn));


/// Unless cond log description and exit
#define KU_ASSERT(cond)                                                 \
    do {                                                                \
        if (!(cond))                                                    \
            ::FailOnAssertion__(#cond,                                  \
                                __FILE__,                               \
                                __LINE__,                               \
                                __PRETTY_FUNCTION__);                   \
    } while (0)

#endif // NDEBUG


namespace ku
{
    /// Class of errors which are to be exposed to JavaScript
    class Error : public std::runtime_error {
    public:
        explicit Error(const std::string& msg)
            : std::runtime_error(msg) {}
    };


    /// Write a message to a log
    void Log(const std::string& message);


    /// Use a specified file for logging instead of cerr
    void OpenLogFile(const std::string& file_name);

    
#ifdef BACKTRACE
    /// Return current backtrace
    std::string Backtrace();
#endif


    /// Write a log message and exit
    void Fail(const std::string& message) __attribute__((noreturn));
}

#endif // ERROR_H
