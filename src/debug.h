
// (c) 2008-2009 by Anton Korenyushkin

/// \file debug.h
/// Debugging stuff

#ifndef DEBUG_H
#define DEBUG_H

#include <string>


#ifdef NDEBUG
#define KU_ASSERT(cond)
#else
#define KU_ASSERT(cond)                                                 \
    do {                                                                \
        if (!(cond))                                                    \
            ku::FailOnAssertion(#cond,                                  \
                                __FILE__,                               \
                                __LINE__,                               \
                                __PRETTY_FUNCTION__);                   \
    } while (0)
#endif


namespace ku
{
    /// Write a message to a log
    void Log(const std::string& message);


    /// Use a specified file for logging instead of cerr
    void OpenLogFile(const std::string& file_name);

    
    /// Return current backtrace
    std::string Backtrace();


    /// Write a log message and exit
    void Fail(const std::string& message) __attribute__((noreturn));
    
    
    /// Assertion fail function
    void FailOnAssertion(const char* cond,
                         const char* file,
                         int line,
                         const char* pretty_function) __attribute__((noreturn));
}

#endif // DEBUG_H
