
// (c) 2009-2010 by Anton Korenyushkin

#ifndef JS_FS_H
#define JS_FS_H

#include "common.h"

#include <v8.h>

#include <sys/stat.h>
#include <memory>


namespace ak
{
    class BaseFile {
    protected:
        int fd_;

        BaseFile(int fd);
        ~BaseFile();

        void Close();
        void CheckOpen() const;
    };


    v8::Handle<v8::Object> InitFS(const std::string& code_path,
                                  const std::string& lib_path,
                                  const std::string& media_path);
}

#endif // JS_FS_H
