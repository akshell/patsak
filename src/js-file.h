
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


    class BinaryBg {
    public:
        DECLARE_JS_CLASS(BinaryBg);

        ~BinaryBg();

        static v8::Handle<v8::Object>
        New(std::auto_ptr<Chars> data_ptr = std::auto_ptr<Chars>());

        static v8::Handle<v8::Object> New(const BinaryBg& parent,
                                          size_t start = 0,
                                          size_t stop = MINUS_ONE);

        const char* GetData() const;
        size_t GetSize() const;

    private:
        class Holder;

        boost::shared_ptr<Holder> holder_ptr_;
        char* data_;
        size_t size_;

        static v8::Handle<v8::Value> ConstructorCb(const v8::Arguments& args);

        BinaryBg(std::auto_ptr<Chars> data_ptr);

        BinaryBg(const BinaryBg& parent,
                 size_t start = 0,
                 size_t stop = MINUS_ONE);

        void SetIndexedProperties(v8::Handle<v8::Object> object) const;
        v8::Handle<v8::Object> Wrap();
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

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, CompareCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, Md5Cb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, Sha1Cb,
                             const v8::Arguments&) const;
    };


    class Binarizator {
    public:
        Binarizator(v8::Handle<v8::Value> value);
        ~Binarizator();
        const char* GetData() const;
        size_t GetSize() const;

    private:
        const BinaryBg* binary_ptr_;
        std::auto_ptr<v8::String::Utf8Value> utf8_value_ptr_;
    };


    class BaseFileBg {
    public:
        DECLARE_JS_CLASS(BaseFileBg);

        virtual void Close();

    protected:
        int fd_;

        BaseFileBg(int fd);
        virtual ~BaseFileBg();
        void CheckOpen() const;

    private:
        DECLARE_JS_CALLBACK2(v8::Handle<v8::Value>, GetClosedCb,
                             v8::Local<v8::String>,
                             const v8::AccessorInfo&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, CloseCb,
                             const v8::Arguments&);
    };


    class FSQuotaChecker;


    class FileBg : public BaseFileBg {
    public:
        DECLARE_JS_CLASS(FileBg);

        FileBg(const std::string& path, FSQuotaChecker* quota_checker_ptr = 0);

    private:
        class ChangeScope;

        FSQuotaChecker* quota_checker_ptr_;

        size_t GetSize() const;

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

        DECLARE_JS_CALLBACK2(v8::Handle<v8::Value>, GetWritableCb,
                             v8::Local<v8::String>,
                             const v8::AccessorInfo&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, FlushCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ReadCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, WriteCb,
                             const v8::Arguments&) const;
    };


    class SocketBg : public BaseFileBg {
    public:
        DECLARE_JS_CLASS(SocketBg);

        SocketBg(int fd);
        SocketBg(const std::string& host, const std::string& service);
        virtual void Close();

    private:
        static const size_t MAX_OPEN_COUNT;
        static size_t open_count;

        static int Connect(const std::string& host, const std::string& service);

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ReceiveCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, SendCb,
                             const v8::Arguments&) const;
    };


    class FSBg {
    public:
        DECLARE_JS_CLASS(FSBg);

        FSBg(const std::string& app_path,
             const std::string& release_path,
             uint64_t quota);

        ~FSBg();

        std::string ReadPath(v8::Handle<v8::Value> value,
                             bool can_be_root = true) const;

    private:
        const std::string app_path_;
        const std::string release_path_;
        boost::scoped_ptr<FSQuotaChecker> quota_checker_ptr_;

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

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ConnectCb,
                             const v8::Arguments&) const;
    };
}

#endif // JS_FILE_H
