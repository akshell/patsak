
// (c) 2008-2010 by Anton Korenyushkin

#ifndef DEBUG_H
#define DEBUG_H

#include <string>
#include <sstream>


namespace ku
{
    extern std::string log_prefix;
    
    void Log(const std::string& message);

    std::string Backtrace();

    void Fail(const std::string& message) __attribute__((noreturn));
    
    void FailOnAssertion(
        const std::string& file,
        int line,
        const std::string& pretty_function,
        const std::string& assertion,
        const std::string& message = "") __attribute__((noreturn));
    
    
    inline void Assert(const std::string& file,
                       int line,
                       const std::string& pretty_function,
                       bool cond,
                       const std::string& cond_str,
                       const std::string& message = "")
    {
        if (!cond)
            FailOnAssertion(file, line, pretty_function, cond_str, message);
    }


    template <typename T1, typename T2>
    inline void AssertEqual(const std::string& file,
                            int line,
                            const std::string& pretty_function,
                            const T1& lhs,
                            const T2& rhs,
                            const std::string& lhs_str,
                            const std::string& rhs_str)
    {
        if (lhs != rhs) {
            std::ostringstream oss;
            oss << lhs << " != " << rhs;
            FailOnAssertion(file, line, pretty_function,
                            lhs_str + " != " + rhs_str, oss.str());
        }
    }
}


#ifdef NDEBUG

#define KU_ASSERT(cond)
#define KU_ASSERT_MESSAGE(cond, message)
#define KU_ASSERT_EQUAL(lhs, rhs)

#else // NDEBUG

#define KU_ASSERT(cond)                                                 \
    ku::Assert(__FILE__, __LINE__, __PRETTY_FUNCTION__, cond, #cond)

#define KU_ASSERT_MESSAGE(cond, message)        \
    ku::Assert(__FILE__, __LINE__, __PRETTY_FUNCTION__, cond, #cond, message)

#define KU_ASSERT_EQUAL(lhs, rhs)                                       \
    ku::AssertEqual(__FILE__, __LINE__, __PRETTY_FUNCTION__,            \
                    lhs, rhs, #lhs, #rhs)

#endif // NDEBUG

#endif // DEBUG_H
