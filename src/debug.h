// (c) 2008-2011 by Anton Korenyushkin

#ifndef DEBUG_H
#define DEBUG_H

#include <string>
#include <sstream>


namespace ak
{
    void Fail(const std::string& message) __attribute__((noreturn));

    void FailOnAssertion(
        const std::string& file,
        int line,
        const std::string& pretty_function,
        const std::string& assertion,
        const std::string& message = "") __attribute__((noreturn));

    void InitDebug(const std::string& log_id);
}


#ifdef NDEBUG

#define AK_ASSERT(cond)                                                 \
    static_cast<void>(cond)

#define AK_ASSERT_MESSAGE(cond, message)                                \
    static_cast<void>(cond), static_cast<void>(message)

#define AK_ASSERT_EQUAL(lhs, rhs)                                       \
    static_cast<void>(lhs), static_cast<void>(rhs)

#else // NDEBUG

#define AK_ASSERT(cond)                                                 \
    ((cond)                                                             \
     ? static_cast<void>(0)                                             \
     : FailOnAssertion(                                                 \
         __FILE__, __LINE__, __PRETTY_FUNCTION__, #cond));              \


#define AK_ASSERT_MESSAGE(cond, message)                                \
    ((cond)                                                             \
     ? static_cast<void>(0)                                             \
     : FailOnAssertion(                                                 \
         __FILE__, __LINE__, __PRETTY_FUNCTION__, #cond, message));     \


#define AK_ASSERT_EQUAL(lhs, rhs)                                       \
    do {                                                                \
        typeof(lhs) lhs_value(lhs);                                     \
        typeof(lhs) rhs_value(rhs);                                     \
        if (lhs_value != rhs_value) {                                   \
            ostringstream oss;                                          \
            oss << lhs_value << " != " << rhs_value;                    \
            FailOnAssertion(                                            \
                __FILE__, __LINE__, __PRETTY_FUNCTION__,                \
                #lhs " == " #rhs, oss.str());                           \
        }                                                               \
    } while (0)

#endif // NDEBUG

#endif // DEBUG_H
