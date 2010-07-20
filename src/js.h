// (c) 2009-2010 by Anton Korenyushkin

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
                const std::string& media_path,
                const std::string& git_path_prefix,
                const std::string& git_path_suffix,
                const std::string& db_options,
                const std::string& schema_name,
                const std::string& tablespace_name,
                bool managed);
}

#endif // JS_H
