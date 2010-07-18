// (c) 2009-2010 by Anton Korenyushkin

#include "js-fs.h"
#include "js-common.h"
#include "js-binary.h"
#include "db.h"

#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <sys/file.h>


using namespace ak;
using namespace v8;
using namespace std;


////////////////////////////////////////////////////////////////////////////////
// Constants
////////////////////////////////////////////////////////////////////////////////

namespace
{
    const unsigned DIRECTORY_SIZE = 4 * 1024;
}

////////////////////////////////////////////////////////////////////////////////
// Utils
////////////////////////////////////////////////////////////////////////////////

namespace
{
    Error MakeErrnoError()
    {
        Error::Tag tag = Error::FS;
        switch (errno) {
        case EEXIST:       tag = Error::ENTRY_EXISTS;  break;
        case ENOENT:       tag = Error::NO_SUCH_ENTRY; break;
        case EISDIR:       tag = Error::ENTRY_IS_DIR;  break;
        case ENOTDIR:      tag = Error::ENTRY_IS_FILE; break;
        case ENAMETOOLONG: tag = Error::VALUE;         break;
        }
        return Error(tag, strerror(errno));
    }


    auto_ptr<struct stat> GetStat(const string& path, bool ignore_error = false)
    {
        auto_ptr<struct stat> result(new struct stat());
        if (stat(path.c_str(), result.get()) != -1)
            return result;
        if (ignore_error)
            return auto_ptr<struct stat>();
        throw MakeErrnoError();
    }
}

////////////////////////////////////////////////////////////////////////////////
// FileBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class FileBg {
    public:
        DECLARE_JS_CLASS(FileBg);

        FileBg(const std::string& path, bool writable);
        ~FileBg();

    private:
        int fd_;
        bool writable_;

        void Close();
        void CheckOpen() const;
        void CheckWritable() const;
        size_t GetSize() const;
        size_t GetPosition() const;

        DECLARE_JS_CALLBACK2(v8::Handle<v8::Value>, GetClosedCb,
                             v8::Local<v8::String>,
                             const v8::AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(v8::Handle<v8::Value>, GetWritableCb,
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

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, LockCb,
                             const v8::Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, UnlockCb,
                             const v8::Arguments&) const;
    };
}


DEFINE_JS_CLASS(FileBg, "File", object_template, proto_template)
{
    object_template->SetAccessor(String::NewSymbol("closed"),
                                 GetClosedCb, 0,
                                 Handle<v8::Value>(), DEFAULT,
                                 ReadOnly | DontDelete);
    object_template->SetAccessor(String::NewSymbol("writable"),
                                 GetWritableCb, 0,
                                 Handle<v8::Value>(), DEFAULT,
                                 ReadOnly | DontDelete);
    object_template->SetAccessor(String::NewSymbol("length"),
                                 GetLengthCb, SetLengthCb,
                                 Handle<v8::Value>(), DEFAULT,
                                 DontDelete);
    object_template->SetAccessor(String::NewSymbol("position"),
                                 GetPositionCb, SetPositionCb,
                                 Handle<v8::Value>(), DEFAULT,
                                 DontDelete);
    SetFunction(proto_template, "close", CloseCb);
    SetFunction(proto_template, "flush", FlushCb);
    SetFunction(proto_template, "read", ReadCb);
    SetFunction(proto_template, "write", WriteCb);
    SetFunction(proto_template, "lock", LockCb);
    SetFunction(proto_template, "unlock", UnlockCb);
}


FileBg::FileBg(const string& path, bool writable)
    : fd_(writable
          ? open(path.c_str(), O_CLOEXEC | O_RDWR | O_CREAT, S_IRUSR | S_IWUSR)
          : open(path.c_str(), O_CLOEXEC | O_RDONLY))
    , writable_(writable)
{
    if (fd_ == -1)
        throw MakeErrnoError();
}


FileBg::~FileBg()
{
    Close();
}


void FileBg::Close()
{
    if (fd_ != -1) {
        close(fd_);
        fd_ = -1;
    }
}


void FileBg::CheckOpen() const
{
    if (fd_ == -1)
        throw Error(Error::VALUE, "File is already closed");
}


void FileBg::CheckWritable() const
{
    CheckOpen();
    if (!writable_)
        throw Error(Error::VALUE, "File is read-only");
}


size_t FileBg::GetSize() const
{
    struct stat st;
    int ret = fstat(fd_, &st);
    AK_ASSERT_EQUAL(ret, 0);
    return st.st_size;
}


size_t FileBg::GetPosition() const
{
    off_t position = lseek(fd_, 0, SEEK_CUR);
    AK_ASSERT(position != -1);
    return position;
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, FileBg, GetClosedCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/) const
{
    return Boolean::New(fd_ == -1);
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, FileBg, GetWritableCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/) const
{
    CheckOpen();
    return Boolean::New(writable_);
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, FileBg, GetLengthCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/) const
{
    CheckOpen();
    return Integer::New(GetSize());
}


DEFINE_JS_CALLBACK3(void, FileBg, SetLengthCb,
                    Local<String>, /*property*/,
                    Local<v8::Value>, value,
                    const AccessorInfo&, /*info*/) const
{
    CheckWritable();
    int ret = ftruncate(fd_, value->Uint32Value());
    AK_ASSERT_EQUAL(ret, 0);
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, FileBg, GetPositionCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/) const
{
    CheckOpen();
    return Integer::New(GetPosition());
}


DEFINE_JS_CALLBACK3(void, FileBg, SetPositionCb,
                    Local<String>, /*property*/,
                    Local<v8::Value>, value,
                    const AccessorInfo&, /*info*/) const
{
    CheckOpen();
    off_t position = lseek(fd_, value->Uint32Value(), SEEK_SET);
    AK_ASSERT(position != -1);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileBg, CloseCb,
                    const Arguments&, /*args*/)
{
    Close();
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileBg, FlushCb,
                    const Arguments&, args) const
{
    CheckWritable();
    int ret = fsync(fd_);
    AK_ASSERT_EQUAL(ret, 0);
    return args.This();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileBg, ReadCb,
                    const Arguments&, args) const
{
    // XXX: races
    CheckOpen();
    size_t rest_size = GetSize() - GetPosition();
    auto_ptr<Chars> data_ptr(
        new Chars(args.Length()
                  ? min(args[0]->Uint32Value(), rest_size)
                  : rest_size));
    ssize_t count = read(fd_, &data_ptr->front(), data_ptr->size());
    AK_ASSERT(count == static_cast<ssize_t>(data_ptr->size()));
    return NewBinary(data_ptr);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileBg, WriteCb,
                    const Arguments&, args) const
{
    CheckWritable();
    CheckArgsLength(args, 1);
    Binarizator binarizator(args[0]);
    ssize_t count = write(fd_, binarizator.GetData(), binarizator.GetSize());
    AK_ASSERT_EQUAL(count, static_cast<ssize_t>(binarizator.GetSize()));
    return args.This();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileBg, LockCb,
                    const Arguments&, args) const
{
    CheckWritable();
    int type;
    if (args.Length()) {
        string type_name(Stringify(args[0]));
        if (type_name == "shared")
            type = LOCK_SH;
        else if (type_name == "exclusive")
            type = LOCK_EX;
        else
            throw Error(Error::VALUE,
                        "Valid lock types are 'shared' and 'exclusive'");
    } else {
        type = LOCK_EX;
    }
    int ret = flock(fd_, type);
    AK_ASSERT_EQUAL(ret, 0);
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileBg, UnlockCb,
                    const Arguments&, /*args*/) const
{
    CheckWritable();
    int ret = flock(fd_, LOCK_UN);
    AK_ASSERT_EQUAL(ret, 0);
    return Undefined();
}

////////////////////////////////////////////////////////////////////////////////
// FileStorageBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class FileStorageBg {
    public:
        DECLARE_JS_CLASS(FileStorageBg);

        FileStorageBg(const string& root_path, bool writable);

    private:
        string root_path_;
        bool writable_;

        void CheckWritable() const;
        string ReadPath(Handle<v8::Value> value, bool can_be_root = true) const;
        string ReadPath(const Arguments& args) const;

        DECLARE_JS_CALLBACK2(v8::Handle<v8::Value>, GetWritableCb,
                             v8::Local<v8::String>,
                             const v8::AccessorInfo&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, OpenCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, ExistsCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, IsDirCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, IsFileCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, GetModDateCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, ListCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, CreateDirCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, RemoveCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, RenameCb,
                             const Arguments&) const;

    };
}


DEFINE_JS_CLASS(FileStorageBg, "FileStorage", object_template, proto_template)
{
    object_template->SetAccessor(String::NewSymbol("writable"),
                                 GetWritableCb, 0,
                                 Handle<v8::Value>(), DEFAULT,
                                 ReadOnly | DontDelete);
    SetFunction(proto_template, "open", OpenCb);
    SetFunction(proto_template, "exists", ExistsCb);
    SetFunction(proto_template, "isDir", IsDirCb);
    SetFunction(proto_template, "isFile", IsFileCb);
    SetFunction(proto_template, "getModDate", GetModDateCb);
    SetFunction(proto_template, "list", ListCb);
    SetFunction(proto_template, "createDir", CreateDirCb);
    SetFunction(proto_template, "remove", RemoveCb);
    SetFunction(proto_template, "rename", RenameCb);
}


FileStorageBg::FileStorageBg(const string& root_path, bool writable)
    : root_path_(root_path)
    , writable_(writable)
{
}


void FileStorageBg::CheckWritable() const
{
    if (!writable_)
        throw Error(Error::VALUE, "File storage is read-only");
}


string FileStorageBg::ReadPath(Handle<v8::Value> value, bool can_be_root) const
{
    string path(Stringify(value));
    size_t depth = 0;
    for (size_t from = 0; from < path.size(); ++from) {
        size_t to = path.find('/', from);
        if (to == from)
            continue;
        string component = path.substr(from, to - from);
        if (component == "..") {
            if (!depth)
                throw Error(
                    Error::VALUE,
                    "Path \"" + path + "\" leads beyond the root directory");
            --depth;
        } else if (component != ".") {
            ++depth;
        }
        if (to == string::npos)
            break;
        from = to;
    }
    if (!can_be_root && !depth)
        throw Error(Error::VALUE, "Path \"" + path + "\" is empty");
    return root_path_ + '/' + path;
}


string FileStorageBg::ReadPath(const Arguments& args) const
{
    CheckArgsLength(args, 1);
    return ReadPath(args[0]);
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, FileStorageBg, GetWritableCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/) const
{
    return Boolean::New(writable_);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileStorageBg, OpenCb,
                    const Arguments&, args) const
{
    return JSNew<FileBg>(ReadPath(args), writable_);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileStorageBg, ExistsCb,
                    const Arguments&, args) const
{
    return Boolean::New(GetStat(ReadPath(args), true).get());
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileStorageBg, IsDirCb,
                    const Arguments&, args) const
{
    auto_ptr<struct stat> stat_ptr(GetStat(ReadPath(args), true));
    return Boolean::New(stat_ptr.get() && S_ISDIR(stat_ptr->st_mode));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileStorageBg, IsFileCb,
                    const Arguments&, args) const
{
    auto_ptr<struct stat> stat_ptr(GetStat(ReadPath(args), true));
    return Boolean::New(stat_ptr.get() && S_ISREG(stat_ptr->st_mode));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileStorageBg, GetModDateCb,
                    const Arguments&, args) const
{
    return Date::New(
        static_cast<double>(GetStat(ReadPath(args))->st_mtime) * 1000);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileStorageBg, ListCb,
                    const Arguments&, args) const
{
    DIR* dir_ptr = opendir(ReadPath(args).c_str());
    if (!dir_ptr)
        throw MakeErrnoError();
    Handle<Array> result(Array::New());
    size_t i = 0;
    while (struct dirent* dirent_ptr = readdir(dir_ptr)) {
        string name(dirent_ptr->d_name);
        if (name != "." && name != "..")
            result->Set(Integer::New(i++), String::New(name.c_str()));
    }
    closedir(dir_ptr);
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileStorageBg, CreateDirCb,
                    const Arguments&, args) const
{
    CheckWritable();
    CheckArgsLength(args, 1);
    if (mkdir(ReadPath(args[0]).c_str(), 0755) == -1)
        throw MakeErrnoError();
    return Undefined();
}


namespace
{
    void DoRemove(const string& path)
    {
        DIR* dir_ptr = opendir(path.c_str());
        if (dir_ptr) {
            while (struct dirent* dirent_ptr = readdir(dir_ptr)) {
                string name(dirent_ptr->d_name);
                if (name != "." && name != "..")
                    DoRemove(path + '/' + name);
            }
            int ret = rmdir(path.c_str());
            AK_ASSERT_EQUAL(ret, 0);
        } else if (errno == ENOTDIR) {
            int ret = unlink(path.c_str());
            AK_ASSERT_EQUAL(ret, 0);
        } else {
            throw MakeErrnoError();
        }
    }
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileStorageBg, RemoveCb,
                    const Arguments&, args) const
{
    CheckWritable();
    CheckArgsLength(args, 1);
    DoRemove(ReadPath(args[0], false));
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileStorageBg, RenameCb,
                    const Arguments&, args) const
{
    CheckWritable();
    CheckArgsLength(args, 2);
    string from_path(ReadPath(args[0], false));
    string to_path(ReadPath(args[1], false));
    if (rename(from_path.c_str(), to_path.c_str()) == -1)
        throw MakeErrnoError();
    return Undefined();
}

////////////////////////////////////////////////////////////////////////////////
// InitFS
////////////////////////////////////////////////////////////////////////////////

Handle<Object> ak::InitFS(const string& code_path,
                          const string& lib_path,
                          const string& media_path)
{
    Handle<Object> result(Object::New());
    PutClass<FileBg>(result);
    PutClass<FileStorageBg>(result);
    Set(result, "code", JSNew<FileStorageBg>(code_path, false));
    Set(result, "lib", JSNew<FileStorageBg>(lib_path, false));
    Set(result, "media", JSNew<FileStorageBg>(media_path, true));
    return result;
}
