// (c) 2010-2011 by Anton Korenyushkin

#ifndef JS_GIT_H
#define JS_GIT_H

#include "common.h"

#include <v8.h>

#include <string>


namespace ak
{
    v8::Handle<v8::Object> InitGit(const GitPathPatterns& path_patterns);
}

#endif // JS_GIT_H
