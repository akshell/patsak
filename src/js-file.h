
// (c) 2009-2010 by Anton Korenyushkin

#ifndef JS_FILE_H
#define JS_FILE_H

#include "common.h"

#include <v8.h>

#include <sys/stat.h>
#include <memory>


namespace ak
{
    std::auto_ptr<Chars> ReadFile(const std::string& path);

    std::auto_ptr<struct stat> GetStat(const std::string& path,
                                       bool ignore_error = false);

    // Calculate the depth of directory nesting for relative or absolute path.
    // Return 0 for empty or root path, -1 for path beyond root or parent.
    int GetPathDepth(const std::string& path);


    class BaseFile {
    protected:
        int fd_;

        BaseFile(int fd);
        ~BaseFile();

        void Close();
        void CheckOpen() const;
    };


    v8::Handle<v8::Object> InitFS(const std::string& media_path);
}

#endif // JS_FILE_H
