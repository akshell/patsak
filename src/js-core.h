
// (c) 2010 by Anton Korenyushkin

#ifndef JS_CORE_H
#define JS_CORE_H

#include <v8.h>

#include <string>


namespace ak
{
    v8::Handle<v8::Object> InitCore(const std::string& code_path);
}

#endif // JS_SCRIPT_H
