// (c) 2009-2011 by Anton Korenyushkin

#ifndef JS_H
#define JS_H

#include "common.h"


namespace ak
{
    bool HandleRequest(int conn_fd);
    bool EvalExpr(const char* expr, size_t size, std::string& result);
    bool ProgramIsDead();

    void InitJS(const std::string& code_path,
                const std::string& lib_path,
                const GitPathPatterns& git_path_patterns,
                const std::string& repo_name,
                const std::string& db_options,
                const std::string& schema_name,
                const std::string& tablespace_name,
                size_t timeout,
                bool managed);
}

#endif // JS_H
