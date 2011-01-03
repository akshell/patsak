// (c) 2009-2011 by Anton Korenyushkin

#ifndef JS_DB_H
#define JS_DB_H

#include <v8.h>


namespace ak
{
    v8::Handle<v8::Object> InitDB();
    bool RolledBack();
}

#endif // JS_DB_H
