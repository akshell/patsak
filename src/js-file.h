
// (c) 2009-2010 by Anton Korenyushkin

#ifndef JS_FILE_H
#define JS_FILE_H

#include "js-common.h"

#include <boost/scoped_ptr.hpp>

#include <sys/stat.h>


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


    class FileBg : private BaseFile {
    public:
        DECLARE_JS_CLASS(FileBg);

        FileBg(const std::string& path);

    private:
        size_t GetSize() const;

        DECLARE_JS_CALLBACK2(v8::Handle<v8::Value>, GetClosedCb,
                             v8::Local<v8::String>,
                             const v8::AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(v8::Handle<v8::Value>, GetLengthCb,
                             v8::Local<v8::String>,
                             const v8::AccessorInfo&) const;

        DECLARE_JS_CALLBACK3(void, SetLengthCb,
                             v8::Local<v8::String>,
                             v8::Local<v8::Value>,
                             const v8::AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(v8::Handle<v8::Value>, GetPositionCb,
                             v8::Local<v8::String>,
                             const v8::AccessorInfo&) const;

        DECLARE_JS_CALLBACK3(void, SetPositionCb,
                             v8::Local<v8::String>,
                             v8::Local<v8::Value>,
                             const v8::AccessorInfo&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, CloseCb,
                             const v8::Arguments&);

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, FlushCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ReadCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, WriteCb,
                             const v8::Arguments&) const;
    };


    class FSBg {
    public:
        DECLARE_JS_CLASS(FSBg);

        FSBg(const std::string& app_path, const std::string& release_path);
        ~FSBg();

        std::string ReadPath(v8::Handle<v8::Value> value,
                             bool can_be_root = true) const;

    private:
        const std::string app_path_;
        const std::string release_path_;

        std::string ReadPath(const v8::Arguments& args) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, OpenCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ExistsCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, IsDirCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, IsFileCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, GetModDateCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ListCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, CreateDirCb,
                             const v8::Arguments&);

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, RemoveCb,
                             const v8::Arguments&);

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, RenameCb,
                             const v8::Arguments&) const;
    };
}

#endif // JS_FILE_H
