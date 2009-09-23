
// (c) 2009 by Anton Korenyushkin

/// \file js-data.h
/// JavaScript binary data handler interface

#ifndef JS_FILE_H
#define JS_FILE_H

#include "js-common.h"
#include "common.h"

#include <boost/shared_ptr.hpp>
#include <boost/utility.hpp>


namespace ku
{
    /// Data background
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


    /// Temprorary file background
    class TempFileBg {
    public:
        DECLARE_JS_CLASS(TempFileBg);

        TempFileBg(const std::string& path);
        std::string GetPath() const;
        void ClearPath();

    private:
        std::string path_;
    };


    /// File system accessor background
    class FSBg {
    public:
        class FileAccessor;
        
        DECLARE_JS_CLASS(FSBg);

        FSBg(const std::string& root_path);
        ~FSBg();
        
    private:
        const std::string root_path_;
        unsigned long long total_size_;

        bool ReadPath(v8::Handle<v8::Value> value,
                      std::string& path,
                      bool can_be_root) const;
        bool CheckTotalSize() const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ReadCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ListCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ExistsCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, IsDirCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, IsFileCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, MakeDirCb,
                             const v8::Arguments&);
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, WriteCb,
                             const v8::Arguments&);
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, RemoveCb,
                             const v8::Arguments&);
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, RenameCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, CopyFileCb,
                             const v8::Arguments&);
    };


    /// Interface for access to full media files pathes.
    /// Controls changes in file size after external operations.
    class FSBg::FileAccessor {
    public:
        FileAccessor(FSBg& fs_bg,
                     const std::vector<v8::Handle<v8::Value> >& values);
        ~FileAccessor();
        bool CheckValid() const;
        const Strings& GetFullPathes() const;

    private:
        FSBg& fs_bg_;
        bool is_valid_;
        unsigned long long initial_size_;
        Strings full_pathes_;
    };
    

    /// Try to read whole file into the vector. Return true on success.
    bool ReadFileData(const std::string& path, Chars& data);


    /// Calculate the depth of directory nesting for relative or absolute path.
    /// Return 0 for empty or root path, -1 for path beyond root or parent.
    int GetPathDepth(const std::string& path);
}

#endif // JS_FILE_H
