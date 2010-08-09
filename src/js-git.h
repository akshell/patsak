// (c) 2010 by Anton Korenyushkin

#ifndef JS_GIT_H
#define JS_GIT_H

#include <v8.h>

#include <string>


namespace ak
{
    v8::Handle<v8::Object> InitGit(const std::string& path_prefix,
                                   const std::string& path_suffix,
                                   const std::string& path_ending);
}

#endif // JS_GIT_H
