
// (c) 2009-2010 by Anton Korenyushkin

#include "js-file.h"
#include "js-binary.h"
#include "db.h"

#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <string.h>


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
// MakeErrnoError
////////////////////////////////////////////////////////////////////////////////

namespace
{
    Error MakeErrnoError()
    {
        Error::Tag tag = Error::FS;
        switch (errno) {
        case EEXIST:       tag = Error::ENTRY_EXISTS;     break;
        case ENOENT:       tag = Error::NO_SUCH_ENTRY;    break;
        case EISDIR:       tag = Error::ENTRY_IS_DIR;     break;
        case ENOTDIR:      tag = Error::ENTRY_IS_NOT_DIR; break;
        case ENAMETOOLONG: tag = Error::PATH;             break;
        case EILSEQ:
        case EINVAL:       tag = Error::CONVERSION;       break;
        }
        return Error(tag, strerror(errno));
    }
}

////////////////////////////////////////////////////////////////////////////////
// Interface functions
////////////////////////////////////////////////////////////////////////////////

auto_ptr<Chars> ak::ReadFile(const string& path)
{
    int fd = open(path.c_str(), O_RDONLY);
    if (fd == -1)
        throw MakeErrnoError();
    struct stat st;
    int ret = fstat(fd, &st);
    AK_ASSERT_EQUAL(ret, 0);
    if (S_ISDIR(st.st_mode)) {
        close(fd);
        throw Error(Error::ENTRY_IS_DIR, "Attempt to read directory");
    }
    AK_ASSERT(S_ISREG(st.st_mode));
    auto_ptr<Chars> result(new Chars(st.st_size));
    ssize_t bytes_readen = read(fd, &result->front(), st.st_size);
    AK_ASSERT_EQUAL(bytes_readen, st.st_size);
    close(fd);
    return result;
}


auto_ptr<struct stat> ak::GetStat(const string& path, bool ignore_error)
{
    auto_ptr<struct stat> result(new struct stat());
    if (stat(path.c_str(), result.get()) != -1)
        return result;
    if (ignore_error)
        return auto_ptr<struct stat>();
    throw MakeErrnoError();
}


int ak::GetPathDepth(const string& path)
{
    int depth = 0;
    for (size_t from = 0; from < path.size(); ++from) {
        size_t to = path.find('/', from);
        if (to == from)
            continue;
        string component = path.substr(from, to - from);
        if (component == "..") {
            if (!depth)
                return -1;
            --depth;
        } else if (component != ".") {
            ++depth;
        }
        if (to == string::npos)
            break;
        from = to;
    }
    return depth;
}

////////////////////////////////////////////////////////////////////////////////
// BaseFile definitions
////////////////////////////////////////////////////////////////////////////////

BaseFile::BaseFile(int fd)
    : fd_(fd)
{
    if (fd == -1)
        throw MakeErrnoError();
}


BaseFile::~BaseFile()
{
    Close();
}


void BaseFile::Close()
{
    if (fd_ != -1) {
        int ret = close(fd_);
        AK_ASSERT_EQUAL(ret, 0);
        fd_ = -1;
    }
}


void BaseFile::CheckOpen() const
{
    if (fd_ == -1)
        throw Error(Error::VALUE, "File is already closed");
}

////////////////////////////////////////////////////////////////////////////////
// FileBg definitions
////////////////////////////////////////////////////////////////////////////////

DEFINE_JS_CLASS(FileBg, "File", object_template, proto_template)
{
    object_template->SetAccessor(String::NewSymbol("closed"),
                                 GetClosedCb, 0,
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
    SetFunction(proto_template, "_close", CloseCb);
    SetFunction(proto_template, "_flush", FlushCb);
    SetFunction(proto_template, "_read", ReadCb);
    SetFunction(proto_template, "_write", WriteCb);
}


FileBg::FileBg(const string& path)
    : BaseFile(
        open(path.c_str(), O_CLOEXEC | O_RDWR | O_CREAT, S_IRUSR | S_IWUSR))
{
}


size_t FileBg::GetSize() const
{
    struct stat st;
    int ret = fstat(fd_, &st);
    AK_ASSERT_EQUAL(ret, 0);
    return st.st_size;
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, FileBg, GetClosedCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/) const
{
    return Boolean::New(fd_ == -1);
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
    CheckOpen();
    int ret = ftruncate(fd_, value->Uint32Value());
    AK_ASSERT_EQUAL(ret, 0);
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, FileBg, GetPositionCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/) const
{
    CheckOpen();
    off_t position = lseek(fd_, 0, SEEK_CUR);
    AK_ASSERT(position != -1);
    return Integer::New(position);
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
    CheckOpen();
    int ret = fsync(fd_);
    AK_ASSERT_EQUAL(ret, 0);
    return args.This();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileBg, ReadCb,
                    const Arguments&, args) const
{
    CheckOpen();
    auto_ptr<Chars> data_ptr(
        new Chars(args.Length() ? args[0]->Uint32Value() : GetSize()));
    ssize_t count = read(fd_, &data_ptr->front(), data_ptr->size());
    AK_ASSERT(count != -1);
    data_ptr->resize(count);
    return NewBinary(data_ptr);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileBg, WriteCb,
                    const Arguments&, args) const
{
    CheckOpen();
    CheckArgsLength(args, 1);
    Binarizator binarizator(args[0]);
    ssize_t count = write(fd_, binarizator.GetData(), binarizator.GetSize());
    AK_ASSERT_EQUAL(count, static_cast<ssize_t>(binarizator.GetSize()));
    return args.This();
}

////////////////////////////////////////////////////////////////////////////////
// FSBg definitions
////////////////////////////////////////////////////////////////////////////////

namespace
{
    uint64_t CalcTotalSize(const string& path)
    {
        struct stat st;
        if (stat(path.c_str(), &st) == -1)
            return 0;
        uint64_t result = st.st_blocks * 512;
        if (S_ISDIR(st.st_mode)) {
            DIR* dir_ptr = opendir(path.c_str());
            AK_ASSERT(dir_ptr);
            while (struct dirent* dirent_ptr = readdir(dir_ptr)) {
                string name(dirent_ptr->d_name);
                if (name != "." && name != "..")
                    result += CalcTotalSize(path + '/' + name);
            }
            closedir(dir_ptr);
        }
        return result;
    }
}


DEFINE_JS_CLASS(FSBg, "FS", object_template, /*proto_template*/)
{
    FileBg::GetJSClass();
    SetFunction(object_template, "open", OpenCb);
    SetFunction(object_template, "exists", ExistsCb);
    SetFunction(object_template, "isDir", IsDirCb);
    SetFunction(object_template, "isFile", IsFileCb);
    SetFunction(object_template, "getModDate", GetModDateCb);
    SetFunction(object_template, "list", ListCb);
    SetFunction(object_template, "createDir", CreateDirCb);
    SetFunction(object_template, "remove", RemoveCb);
    SetFunction(object_template, "rename", RenameCb);
}


FSBg::FSBg(const string& app_path, const string& release_path)
    : app_path_(app_path)
    , release_path_(release_path)
{
}


FSBg::~FSBg()
{
}


string FSBg::ReadPath(Handle<v8::Value> value, bool can_be_root) const
{
    string rel_path(Stringify(value));
    int depth = GetPathDepth(rel_path);
    if (depth < 0)
        throw Error(Error::PATH,
                    ("Path \"" + rel_path +
                     "\" leads beyond the root directory"));
    if (!can_be_root && !depth)
        throw Error(Error::PATH, "Path \"" + rel_path + "\" is empty");
    return app_path_ + '/' + rel_path;
}


string FSBg::ReadPath(const Arguments& args) const
{
    CheckArgsLength(args, 1);
    return ReadPath(args[0]);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, OpenCb,
                    const Arguments&, args) const
{
    return JSNew<FileBg>(ReadPath(args));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, ExistsCb,
                    const Arguments&, args) const
{
    return Boolean::New(GetStat(ReadPath(args), true).get());
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, IsDirCb,
                    const Arguments&, args) const
{
    auto_ptr<struct stat> stat_ptr(GetStat(ReadPath(args), true));
    return Boolean::New(stat_ptr.get() && S_ISDIR(stat_ptr->st_mode));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, IsFileCb,
                    const Arguments&, args) const
{
    auto_ptr<struct stat> stat_ptr(GetStat(ReadPath(args), true));
    return Boolean::New(stat_ptr.get() && S_ISREG(stat_ptr->st_mode));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, GetModDateCb,
                    const Arguments&, args) const
{
    return Date::New(
        static_cast<double>(GetStat(ReadPath(args))->st_mtime) * 1000);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, ListCb,
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


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, CreateDirCb,
                    const Arguments&, args)
{
    CheckArgsLength(args, 1);
    if (mkdir(ReadPath(args[0]).c_str(), 0755) == -1)
        throw MakeErrnoError();
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, RemoveCb,
                    const Arguments&, args)
{
    CheckArgsLength(args, 1);
    if (remove(ReadPath(args[0], false).c_str()) == -1)
        throw MakeErrnoError();
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, RenameCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 2);
    string from_path(ReadPath(args[0], false));
    string to_path(ReadPath(args[1], false));
    if (rename(from_path.c_str(), to_path.c_str()) == -1)
        throw MakeErrnoError();
    return Undefined();
}
