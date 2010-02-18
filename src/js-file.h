
// (c) 2009-2010 by Anton Korenyushkin

#ifndef JS_FILE_H
#define JS_FILE_H

#include "js-common.h"

#include <boost/shared_ptr.hpp>

#include <sys/stat.h>


namespace ku
{
    Chars ReadFileData(const std::string& path);

    std::auto_ptr<struct stat> GetStat(const std::string& path,
                                       bool ignore_error = false);

    // Calculate the depth of directory nesting for relative or absolute path.
    // Return 0 for empty or root path, -1 for path beyond root or parent.
    int GetPathDepth(const std::string& path);

    
    class DataBg {
    public:
        DECLARE_JS_CLASS(DataBg);

        DataBg(std::auto_ptr<Chars> data_ptr);
        ~DataBg();
        const Chars& GetData() const;

    private:
        boost::shared_ptr<Chars> data_ptr_;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ToStringCb,
                             const v8::Arguments&) const;
    };


    class TempFileBg {
    public:
        DECLARE_JS_CLASS(TempFileBg);

        TempFileBg(const std::string& path);
        std::string GetPath() const;
        void ClearPath();

    private:
        std::string path_;
    };


    class FSBg {
    public:
        class FileAccessor;
        
        DECLARE_JS_CLASS(FSBg);

        FSBg(const std::string& root_path, unsigned long long quota);
        ~FSBg();
        
    private:
        const std::string root_path_;
        const unsigned long long quota_;
        unsigned long long total_size_;

        std::string ReadPath(v8::Handle<v8::Value> value,
                             bool can_be_root) const;
        void CheckTotalSize(unsigned long long addition) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ReadCb,
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
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, MakeDirCb,
                             const v8::Arguments&);
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, WriteCb,
                             const v8::Arguments&);
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, RemoveCb,
                             const v8::Arguments&);
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, RenameCb,
                             const v8::Arguments&) const;
    };


    // Interface for access to full media file pathes.
    // Controls changes in file size after external operations.
    class FSBg::FileAccessor {
    public:
        FileAccessor(FSBg& fs_bg,
                     const std::vector<v8::Handle<v8::Value> >& values);
        ~FileAccessor();
        const Strings& GetFullPathes() const;

    private:
        FSBg& fs_bg_;
        unsigned long long initial_size_;
        Strings full_pathes_;
    };
}

#endif // JS_FILE_H
