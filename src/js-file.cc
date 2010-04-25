
// (c) 2009-2010 by Anton Korenyushkin

#include "js-file.h"

#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>

#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <iconv.h>


using namespace ku;
using namespace v8;
using namespace std;
using boost::lexical_cast;


////////////////////////////////////////////////////////////////////////////////
// Constants
////////////////////////////////////////////////////////////////////////////////

namespace
{
    const int MAX_DIR_DEPTH = 30;
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
        case ENOTEMPTY:    tag = Error::DIR_IS_NOT_EMPTY; break;
        case EILSEQ:
        case EINVAL:       tag = Error::CONVERSION;       break;
        }
        return Error(tag, strerror(errno));
    }
}

////////////////////////////////////////////////////////////////////////////////
// Interface functions
////////////////////////////////////////////////////////////////////////////////

Chars ku::ReadFileData(const std::string& path)
{
    int fd = open(path.c_str(), O_RDONLY);
    if (fd == -1)
        throw MakeErrnoError();
    struct stat st;
    int ret = fstat(fd, &st);
    KU_ASSERT_EQUAL(ret, 0);
    if (S_ISDIR(st.st_mode)) {
        close(fd);
        throw Error(Error::ENTRY_IS_DIR, "Attempt to read directory");
    }
    KU_ASSERT(S_ISREG(st.st_mode));
    Chars result(st.st_size);
    ssize_t bytes_readen = read(fd, &result[0], st.st_size);
    KU_ASSERT_EQUAL(bytes_readen, st.st_size);
    close(fd);
    return result;
}


auto_ptr<struct stat> ku::GetStat(const string& path, bool ignore_error)
{
    auto_ptr<struct stat> result(new struct stat());
    if (stat(path.c_str(), result.get()) != -1)
        return result;
    if (ignore_error)
        return auto_ptr<struct stat>();
    throw MakeErrnoError();
}


int ku::GetPathDepth(const std::string& path)
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
// DataStringResource
////////////////////////////////////////////////////////////////////////////////

// TODO: re-enable it

// namespace
// {
//     class DataStringResource : public String::ExternalAsciiStringResource {
//     public:
//         DataStringResource(shared_ptr<Chars> data_ptr);
//         virtual const char* data() const;
//         virtual size_t length() const;
        
//     private:
//         shared_ptr<Chars> data_ptr_;
//     };
// }


// DataStringResource::DataStringResource(shared_ptr<Chars> data_ptr)
//     : data_ptr_(data_ptr)
// {
//     KU_ASSERT(data_ptr_);
// }


// const char* DataStringResource::data() const
// {
//     return &(data_ptr_->front());
// }


// size_t DataStringResource::length() const
// {
//     return data_ptr_->size();
// }

////////////////////////////////////////////////////////////////////////////////
// DataBg definitions
////////////////////////////////////////////////////////////////////////////////

DEFINE_JS_CLASS(DataBg, "Data", /*object_template*/, proto_template)
{
    SetFunction(proto_template, "_toString", ToStringCb);
}


DataBg::DataBg(std::auto_ptr<Chars> data_ptr)
    : data_ptr_(data_ptr.release())
{
    KU_ASSERT(data_ptr_);
    V8::AdjustAmountOfExternalAllocatedMemory(data_ptr_->size());
}


DataBg::~DataBg()
{
    V8::AdjustAmountOfExternalAllocatedMemory(-data_ptr_->size());
}


const Chars& DataBg::GetData() const
{
    return *data_ptr_;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DataBg, ToStringCb,
                    const Arguments&, args) const
{
    // TODO NewExternal seems to be broken in new v8 (trunk rev. 2780)
    // May be DataStringResource should be re-enabled for optimization reasons

    CheckArgsLength(args, 1);
    string encoding(Stringify(args[0]));
    iconv_t cd = iconv_open("UTF-16LE", encoding.c_str());
    if (cd == reinterpret_cast<iconv_t>(-1))
        throw (errno == EINVAL
               ? Error(Error::USAGE,
                       "Conversion from \"" + encoding + "\" is not supported")
               : MakeErrnoError());
    Chars buf(data_ptr_->size() * 2);
    char* in_ptr = &data_ptr_->front();
    size_t in_left = data_ptr_->size();
    char* out_ptr = &buf[0];
    size_t out_left = buf.size();
    size_t ret = iconv(cd, &in_ptr, &in_left, &out_ptr, &out_left);
    iconv_close(cd);
    if (ret == MINUS_ONE)
        throw MakeErrnoError();
    size_t length = buf.size() - out_left;
    KU_ASSERT(length % 2 == 0);
    return String::New(reinterpret_cast<uint16_t*>(&buf[0]), length / 2);
}

////////////////////////////////////////////////////////////////////////////////
// TempFileBg definitions
////////////////////////////////////////////////////////////////////////////////

DEFINE_JS_CLASS(TempFileBg, "TempFile", /*object_template*/, /*proto_template*/)
{
}


TempFileBg::TempFileBg(const string& path)
    : path_(path)
{
}


string TempFileBg::GetPath() const
{
    return path_;
}


void TempFileBg::ClearPath()
{
    path_.clear();
}

////////////////////////////////////////////////////////////////////////////////
// FSBg definitions
////////////////////////////////////////////////////////////////////////////////

namespace
{
    unsigned long long CalcTotalSize(const string& path)
    {
        struct stat st;
        if (stat(path.c_str(), &st) == -1)
            return 0;
        unsigned long long result = st.st_blocks * 512;
        if (S_ISDIR(st.st_mode)) {
            DIR* dir_ptr = opendir(path.c_str());
            KU_ASSERT(dir_ptr);
            while (struct dirent* dirent_ptr = readdir(dir_ptr)) {
                string name(dirent_ptr->d_name);
                if (name != "." && name != "..")
                    result += CalcTotalSize(path + '/' + name);
            }
            closedir(dir_ptr);
        }
        return result;
    }


    unsigned long long GetFileSize(const string& path)
    {
        struct stat st;
        if (stat(path.c_str(), &st) == -1)
            return 0;
        return st.st_blocks * 512;
    }
    
    
    void MarkTempFileRemoved(Handle<v8::Value> value)
    {
        TempFileBg* tmp_file_bg_ptr = TempFileBg::GetJSClass().Cast(value);
        if (tmp_file_bg_ptr)
            tmp_file_bg_ptr->ClearPath();
    }
}


DEFINE_JS_CLASS(FSBg, "FS", /*object_template*/, proto_template)
{
    TempFileBg::GetJSClass();
    DataBg::GetJSClass();
    SetFunction(proto_template, "read", ReadCb);
    SetFunction(proto_template, "exists", ExistsCb);
    SetFunction(proto_template, "isDir", IsDirCb);
    SetFunction(proto_template, "isFile", IsFileCb);
    SetFunction(proto_template, "getModDate", GetModDateCb);
    SetFunction(proto_template, "list", ListCb);
    SetFunction(proto_template, "createDir", CreateDirCb);
    SetFunction(proto_template, "write", WriteCb);
    SetFunction(proto_template, "remove", RemoveCb);
    SetFunction(proto_template, "rename", RenameCb);
}


FSBg::FSBg(const string& root_path, unsigned long long quota)
    : root_path_(root_path)
    , quota_(quota)
    , total_size_(CalcTotalSize(root_path))
{
}


FSBg::~FSBg()
{
}


string FSBg::ReadPath(Handle<v8::Value> value, bool can_be_root) const
{
    TempFileBg* temp_file_bg_ptr = TempFileBg::GetJSClass().Cast(value);
    if (temp_file_bg_ptr) {
        string result(temp_file_bg_ptr->GetPath());
        if (result.empty())
            throw Error(Error::TEMP_FILE_REMOVED,
                        "Temp file is already removed");
        return result;
    }
    string rel_path(Stringify(value));
    int depth = GetPathDepth(rel_path);
    if (depth < 0)
        throw Error(Error::PATH,
                    ("Path \"" + rel_path +
                     "\" leads beyond the root directory"));
    if (!can_be_root && !depth)
        throw Error(Error::PATH, "Path \"" + rel_path + "\" is empty");
    if (depth > MAX_DIR_DEPTH)
        throw Error(Error::PATH,
                    ("Maximum directory depth is " +
                     lexical_cast<string>(MAX_DIR_DEPTH)));
    return root_path_ + '/' + rel_path;
}


void FSBg::CheckTotalSize(unsigned long long addition) const
{
    if (total_size_ + addition > quota_)
        throw Error(Error::FS_QUOTA, "File storage quota exceeded");
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, ReadCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    string path(ReadPath(args[0], false));
    return JSNew<DataBg>(auto_ptr<Chars>(new Chars(ReadFileData(path))));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, ExistsCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    string path(ReadPath(args[0], true));
    return Boolean::New(GetStat(path, true).get());
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, IsDirCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    string path(ReadPath(args[0], true));
    auto_ptr<struct stat> stat_ptr(GetStat(path, true));
    return Boolean::New(stat_ptr.get() && S_ISDIR(stat_ptr->st_mode));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, IsFileCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    string path(ReadPath(args[0], true));
    auto_ptr<struct stat> stat_ptr(GetStat(path, true));
    return Boolean::New(stat_ptr.get() && S_ISREG(stat_ptr->st_mode));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, GetModDateCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    string path(ReadPath(args[0], true));
    return Date::New(static_cast<double>(GetStat(path)->st_mtime) * 1000);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, ListCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    string path(ReadPath(args[0], true));
    DIR* dir_ptr = opendir(path.c_str());
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
    CheckTotalSize(DIRECTORY_SIZE);
    string path(ReadPath(args[0], false));
    if (mkdir(path.c_str(), 0755) == -1)
        throw MakeErrnoError();
    total_size_ += DIRECTORY_SIZE;
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, WriteCb,
                    const Arguments&, args)
{
    CheckArgsLength(args, 2);
    string path(ReadPath(args[0], false));
    unsigned long long old_size = GetFileSize(path);
    
    DataBg* data_bg_ptr = DataBg::GetJSClass().Cast(args[1]);
    auto_ptr<String::Utf8Value> utf8_value_ptr;
    const char* data_ptr;
    size_t size;
    if (data_bg_ptr) {
        data_ptr = &(data_bg_ptr->GetData()[0]);
        size = data_bg_ptr->GetData().size();
    } else {
        utf8_value_ptr.reset(new String::Utf8Value(args[1]));
        data_ptr = **utf8_value_ptr;
        size = utf8_value_ptr->length();
    }
    if (size > old_size)
        CheckTotalSize(size - old_size);

    int fd = creat(path.c_str(), 0644);
    if (fd == -1)
        throw MakeErrnoError();
    ssize_t bytes_written = write(fd, data_ptr, size);
    KU_ASSERT_EQUAL(static_cast<size_t>(bytes_written), size);
    close(fd);

    total_size_ += GetFileSize(path);
    total_size_ -= old_size;
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, RemoveCb,
                    const Arguments&, args)
{
    CheckArgsLength(args, 1);
    string path(ReadPath(args[0], false));
    unsigned long long size = GetFileSize(path);
    if (remove(path.c_str()) == -1)
        throw MakeErrnoError();
    total_size_ -= size;
    MarkTempFileRemoved(args[0]);
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
    MarkTempFileRemoved(args[0]);
    return Undefined();
}

////////////////////////////////////////////////////////////////////////////////
// FSBg::FileAccessor definitions
////////////////////////////////////////////////////////////////////////////////

FSBg::FileAccessor::FileAccessor(FSBg& fs_bg,
                                 const vector<Handle<v8::Value> >& values)
    : fs_bg_(fs_bg)
    , initial_size_(0)
{
    full_pathes_.reserve(values.size());
    BOOST_FOREACH(const Handle<v8::Value>& value, values) {
        string full_path(fs_bg.ReadPath(value, false));
        struct stat st;
        if (stat(full_path.c_str(), &st) == -1)
            throw Error(Error::NO_SUCH_ENTRY, "File does not exist");
        if (!S_ISREG(st.st_mode))
            throw Error(Error::ENTRY_IS_DIR, "Directory could not be passed");
        initial_size_ += st.st_blocks * 512;
        full_pathes_.push_back(full_path);
    }
}


FSBg::FileAccessor::~FileAccessor()
{
    unsigned long long size = 0;
    BOOST_FOREACH(const string& full_path, full_pathes_)
        size += GetFileSize(full_path);
    fs_bg_.total_size_ += size;
    fs_bg_.total_size_ -= initial_size_;
}


const Strings& FSBg::FileAccessor::GetFullPathes() const
{
    return full_pathes_;
}
