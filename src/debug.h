
// (c) 2008-2010 by Anton Korenyushkin

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
    // Prefix for all log messages
    extern std::string log_prefix;
    
    void Log(const std::string& message);

    std::string Backtrace();

    // Write a log message and exit
    void Fail(const std::string& message) __attribute__((noreturn));
    
    // Assertion fail function
    void FailOnAssertion(const char* cond,
                         const char* file,
                         int line,
                         const char* pretty_function) __attribute__((noreturn));
}

#endif // DEBUG_H
