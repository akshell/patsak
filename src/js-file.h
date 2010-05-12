
// (c) 2009-2010 by Anton Korenyushkin

#ifndef JS_FILE_H
#define JS_FILE_H

#include "js-common.h"

#include <boost/shared_ptr.hpp>

#include <sys/stat.h>


namespace ku
{
    std::auto_ptr<Chars> ReadFileData(const std::string& path);

    std::auto_ptr<struct stat> GetStat(const std::string& path,
                                       bool ignore_error = false);

    // Calculate the depth of directory nesting for relative or absolute path.
    // Return 0 for empty or root path, -1 for path beyond root or parent.
    int GetPathDepth(const std::string& path);

    
    class BinaryBg {
    public:
        DECLARE_JS_CLASS(BinaryBg);

        class Reader {
        public:
            Reader(v8::Handle<v8::Value> value);
            ~Reader();
            const char* GetStartPtr() const;
            size_t GetSize() const;

        private:
            const BinaryBg* binary_ptr_;
            std::auto_ptr<v8::String::Utf8Value> utf8_value_ptr_;
        };

        BinaryBg(std::auto_ptr<Chars> data_ptr = std::auto_ptr<Chars>());
        
        BinaryBg(const BinaryBg& parent,
                 size_t start = 0,
                 size_t stop = MINUS_ONE);
        
        ~BinaryBg();

    private:
        class Holder;
        
        boost::shared_ptr<Holder> holder_ptr_;
        char* start_ptr_;
        size_t size_;
        
        static v8::Handle<v8::Value> ConstructorCb(const v8::Arguments& args);

        size_t ReadIndex(v8::Handle<v8::Value> value) const;
        
        DECLARE_JS_CALLBACK2(v8::Handle<v8::Value>, GetLengthCb,
                             v8::Local<v8::String>,
                             const v8::AccessorInfo&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ToStringCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, RangeCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, FillCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, IndexOfCb,
                             const v8::Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, LastIndexOfCb,
                             const v8::Arguments&) const;
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
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, CreateDirCb,
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
        FileAccessor(FSBg& fs_bg, v8::Handle<v8::Array> files);
        ~FileAccessor();
        const Strings& GetFullPathes() const;

    private:
        FSBg& fs_bg_;
        unsigned long long initial_size_;
        Strings full_pathes_;
    };
}

#endif // JS_FILE_H
