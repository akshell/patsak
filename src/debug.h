// (c) 2008-2010 by Anton Korenyushkin

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

#define AK_ASSERT(cond)
#define AK_ASSERT_MESSAGE(cond, message)
#define AK_ASSERT_EQUAL(lhs, rhs)

#else // NDEBUG

#define AK_ASSERT(cond)                                                 \
    do {                                                                \
        if (!(cond))                                                    \
            FailOnAssertion(                                            \
                __FILE__, __LINE__, __PRETTY_FUNCTION__, #cond);        \
    } while (0)


#define AK_ASSERT_MESSAGE(cond, message)                                \
    do {                                                                \
        if (!(cond))                                                    \
            FailOnAssertion(                                            \
                __FILE__, __LINE__, __PRETTY_FUNCTION__,                \
                #cond, message);                                        \
    } while (0)


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
