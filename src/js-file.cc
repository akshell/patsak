
// (c) 2009 by Anton Korenyushkin

/// \file js-data.cc
/// JavaScript binary data handler impl

#include "js-file.h"

#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>


using namespace ku;
using namespace v8;
using namespace std;
using boost::shared_ptr;
using boost::lexical_cast;


////////////////////////////////////////////////////////////////////////////////
// Constants
////////////////////////////////////////////////////////////////////////////////

namespace
{
    const int MAX_DIR_DEPTH = 30;
    const unsigned DIRECTORY_SIZE = 4 * 1024;
    const unsigned long long MAX_TOTAL_SIZE = 10 * 1024 * 1024;
    const unsigned long long MAX_FILE_SIZE = 4 * 1024 * 1024;
}

////////////////////////////////////////////////////////////////////////////////
// Macroses
////////////////////////////////////////////////////////////////////////////////

#define JS_ERRNO_CHECK(cond)                    \
    do {                                        \
        if (!(cond)) {                          \
            JS_THROW(Error, strerror(errno));   \
            return false;                       \
        }                                       \
    } while (0)

////////////////////////////////////////////////////////////////////////////////
// DataStringResource
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class DataStringResource : public String::ExternalAsciiStringResource {
    public:
        DataStringResource(shared_ptr<Chars> data_ptr);
        virtual const char* data() const;
        virtual size_t length() const;
        
    private:
        shared_ptr<Chars> data_ptr_;
    };
}


DataStringResource::DataStringResource(shared_ptr<Chars> data_ptr)
    : data_ptr_(data_ptr)
{
    KU_ASSERT(data_ptr_);
}


const char* DataStringResource::data() const
{
    return &(data_ptr_->front());
}


size_t DataStringResource::length() const
{
    return data_ptr_->size();
}

////////////////////////////////////////////////////////////////////////////////
// DataBg definitions
////////////////////////////////////////////////////////////////////////////////

DEFINE_JS_CLASS(DataBg, "Data", /*object_template*/, proto_template)
{
    proto_template->Set(String::NewSymbol("toString"),
                        FunctionTemplate::New(ToStringCb),
                        DontEnum);
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
    JS_CHECK_LENGTH(args, 0);
    return String::NewExternal(new DataStringResource(data_ptr_));
}

////////////////////////////////////////////////////////////////////////////////
// TmpFileBg definitions
////////////////////////////////////////////////////////////////////////////////

DEFINE_JS_CLASS(TmpFileBg, "TmpFile", /*object_template*/, /*proto_template*/)
{
}


TmpFileBg::TmpFileBg(const string& path)
    : path_(path)
{
}


string TmpFileBg::GetPath() const
{
    return path_;
}


void TmpFileBg::ClearPath()
{
    path_.clear();
}

////////////////////////////////////////////////////////////////////////////////
// FSManager
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class FSManager {
    public:
        FSManager(const string& path);
        bool Exists() const;
        bool IsDir() const;
        bool IsFile() const;
        bool List(Strings& items) const;
        bool MkDir() const;
        bool Write(const char* data_ptr, size_t size) const;
        bool Rm() const;
        bool Rename(const string& dest) const;
        bool CopyFile(const string& dest) const;
        
    private:
        string path_;

        auto_ptr<struct stat> GetStat() const;
    };
}


FSManager::FSManager(const string& path)
    : path_(path)
{
}


bool FSManager::Exists() const
{
    return GetStat().get();
}


bool FSManager::IsDir() const
{
    auto_ptr<struct stat> stat_ptr(GetStat());
    if (!stat_ptr.get())
        return false;
    return S_ISDIR(stat_ptr->st_mode);
}


bool FSManager::IsFile() const
{
    auto_ptr<struct stat> stat_ptr(GetStat());
    if (!stat_ptr.get())
        return false;
    KU_ASSERT(S_ISDIR(stat_ptr->st_mode) || S_ISREG(stat_ptr->st_mode));
    return S_ISREG(stat_ptr->st_mode);
}


bool FSManager::List(Strings& items) const
{
    items.clear();
    DIR* dir_ptr = opendir(path_.c_str());
    JS_ERRNO_CHECK(dir_ptr);
    while (struct dirent* dirent_ptr = readdir(dir_ptr)) {
        string item(dirent_ptr->d_name);
        if (item != "." && item != "..")
            items.push_back(item);
    }
    closedir(dir_ptr);
    return true;
}


bool FSManager::MkDir() const
{
    JS_ERRNO_CHECK(!mkdir(path_.c_str(), S_IRWXU));
    return true;
}


bool FSManager::Write(const char* data_ptr, size_t size) const
{
    int fd = creat(path_.c_str(), S_IRUSR | S_IWUSR);
    JS_ERRNO_CHECK(fd != -1);
    ssize_t bytes_written = write(fd, data_ptr, size);
    KU_ASSERT(static_cast<size_t>(bytes_written) == size);
    close(fd);
    return true;
}


bool FSManager::Rm() const
{
    JS_ERRNO_CHECK(!remove(path_.c_str()));
    return true;
}


bool FSManager::Rename(const string& dest) const
{
    JS_ERRNO_CHECK(!rename(path_.c_str(), dest.c_str()));
    return true;
}


bool FSManager::CopyFile(const string& dest) const
{
    Chars data;
    if (!ReadFileData(path_, data))
        return false;
    if (!FSManager(dest).Write(&data[0], data.size()))
        return false;
    return true;
}


auto_ptr<struct stat> FSManager::GetStat() const
{
    auto_ptr<struct stat> result(new struct stat());
    if (stat(path_.c_str(), result.get()) == -1)
        return auto_ptr<struct stat>();
    return result;
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
    
    
    void MarkTmpFileRemoved(Handle<v8::Value> value)
    {
        TmpFileBg* tmp_file_bg_ptr = TmpFileBg::GetJSClass().Cast(value);
        if (tmp_file_bg_ptr)
            tmp_file_bg_ptr->ClearPath();
    }
}


DEFINE_JS_CLASS(FSBg, "FS", /*object_template*/, proto_template)
{
    TmpFileBg::GetJSClass();
    DataBg::GetJSClass();
    proto_template->Set("read", FunctionTemplate::New(ReadCb));
    proto_template->Set("list", FunctionTemplate::New(ListCb));
    proto_template->Set("exists", FunctionTemplate::New(ExistsCb));
    proto_template->Set("isDir", FunctionTemplate::New(IsDirCb));
    proto_template->Set("isFile", FunctionTemplate::New(IsFileCb));
    proto_template->Set("mkdir", FunctionTemplate::New(MkDirCb));
    proto_template->Set("write", FunctionTemplate::New(WriteCb));
    proto_template->Set("rm", FunctionTemplate::New(RmCb));
    proto_template->Set("rename", FunctionTemplate::New(RenameCb));
    proto_template->Set("copyFile", FunctionTemplate::New(CopyFileCb));
}


FSBg::FSBg(const string& root_path)
    : root_path_(root_path)
    , total_size_(CalcTotalSize(root_path))
{
}


FSBg::~FSBg()
{
}


bool FSBg::ReadPath(Handle<v8::Value> value,
                    string& path,
                    bool can_be_root) const
{
    TmpFileBg* tmp_file_bg_ptr = TmpFileBg::GetJSClass().Cast(value);
    if (tmp_file_bg_ptr) {
        path = tmp_file_bg_ptr->GetPath();
        if (path.empty()) {
            JS_THROW(Error, "Temp file is already removed");
            return false;
        }
        return true;
    }
    string rel_path(Stringify(value));
    int depth = GetPathDepth(rel_path);
    if (depth < 0) {
        JS_THROW(Error,
                 "Path " + rel_path + " leads beyond the root directory");
        return false;
    }
    if (!can_be_root && !depth) {
        JS_THROW(Error, "Path " + rel_path + " is empty");
        return false;
    }
    if (depth > MAX_DIR_DEPTH) {
        JS_THROW(Error,
                 "Maximum directory depth is " +
                 lexical_cast<string>(MAX_DIR_DEPTH));
        return false;
    }
    path = root_path_ + '/' + rel_path;
    return true;
}


bool FSBg::CheckTotalSize() const
{
    if (total_size_ < MAX_TOTAL_SIZE)
        return true;
    JS_THROW(Error,
             "File storage quota exceeded, write and mkdir are forbidden" +
             lexical_cast<string>(total_size_));
    return false;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, ReadCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 1);
    string path;
    JS_CAN_THROW(ReadPath(args[0], path, false));
    auto_ptr<Chars> data_ptr(new Chars());
    JS_CAN_THROW(ReadFileData(path, *data_ptr));
    return JSNew<DataBg>(data_ptr);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, ListCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 1);
    string path;
    JS_CAN_THROW(ReadPath(args[0], path, true));
    Strings items;
    JS_CAN_THROW(FSManager(path).List(items));
    Handle<Array> result(Array::New());
    for (size_t i = 0; i < items.size(); ++i)
        result->Set(Integer::New(i), String::New(items[i].c_str()));
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, ExistsCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 1);
    string path;
    JS_CAN_THROW(ReadPath(args[0], path, true));
    return Boolean::New(FSManager(path).Exists());
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, IsDirCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 1);
    string path;
    JS_CAN_THROW(ReadPath(args[0], path, true));
    return Boolean::New(FSManager(path).IsDir());
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, IsFileCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 1);
    string path;
    JS_CAN_THROW(ReadPath(args[0], path, true));
    return Boolean::New(FSManager(path).IsFile());
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, MkDirCb,
                    const Arguments&, args)
{
    JS_CHECK_LENGTH(args, 1);
    JS_CAN_THROW(CheckTotalSize());
    string path;
    JS_CAN_THROW(ReadPath(args[0], path, false));
    JS_CAN_THROW(FSManager(path).MkDir());
    total_size_ += DIRECTORY_SIZE;
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, WriteCb,
                    const Arguments&, args)
{
    JS_CHECK_LENGTH(args, 2);
    JS_CAN_THROW(CheckTotalSize());
    string path;
    JS_CAN_THROW(ReadPath(args[0], path, false));
    unsigned long long old_size = GetFileSize(path);
    
    DataBg* data_bg_ptr = DataBg::GetJSClass().Cast(args[1]);
    auto_ptr<String::AsciiValue> ascii_value_ptr;
    const char* data_ptr;
    size_t size;
    if (data_bg_ptr) {
        data_ptr = &(data_bg_ptr->GetData()[0]);
        size = data_bg_ptr->GetData().size();
    } else {
        ascii_value_ptr.reset(new String::AsciiValue(args[1]));
        data_ptr = **ascii_value_ptr;
        size = ascii_value_ptr->length();
    }
    
    JS_CHECK(size < MAX_FILE_SIZE, ("Max file size is " +
                                    lexical_cast<string>(MAX_FILE_SIZE) +
                                    " bytes"));
    JS_CAN_THROW(FSManager(path).Write(data_ptr, size));
    total_size_ += GetFileSize(path);
    total_size_ -= old_size;
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, RmCb,
                    const Arguments&, args)
{
    JS_CHECK_LENGTH(args, 1);
    string path;
    JS_CAN_THROW(ReadPath(args[0], path, false));
    unsigned long long size = GetFileSize(path);
    JS_CAN_THROW(FSManager(path).Rm());
    total_size_ -= size;
    MarkTmpFileRemoved(args[0]);
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, RenameCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 2);
    string from_path, to_path;
    JS_CAN_THROW(ReadPath(args[0], from_path, false) &&
                 ReadPath(args[1], to_path, false) &&
                 FSManager(from_path).Rename(to_path));
    MarkTmpFileRemoved(args[0]);
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, CopyFileCb,
                    const Arguments&, args)
{
    JS_CHECK_LENGTH(args, 2);
    string from_path, to_path;
    JS_CAN_THROW(ReadPath(args[0], from_path, false) &&
                 ReadPath(args[1], to_path, false) &&
                 FSManager(from_path).CopyFile(to_path));
    total_size_ += GetFileSize(to_path);
    return Undefined();
}

////////////////////////////////////////////////////////////////////////////////
// FSBg::FileAccessor definitions
////////////////////////////////////////////////////////////////////////////////

FSBg::FileAccessor::FileAccessor(FSBg& fs_bg,
                                 const vector<Handle<v8::Value> >& values)
    : fs_bg_(fs_bg)
    , is_valid_(false)
    , initial_size_(0)
{
    full_pathes_.reserve(values.size());
    BOOST_FOREACH(const Handle<v8::Value>& value, values) {
        string full_path;
        if (!fs_bg.ReadPath(value, full_path, false))
            return;
        struct stat st;
        if (stat(full_path.c_str(), &st) == -1) {
            JS_THROW(Error, "File does not exist");
            return;
        }
        if (!S_ISREG(st.st_mode)) {
            JS_THROW(Error, "Directory could not be passed");
            return;
        }
        initial_size_ += st.st_blocks * 512;
        full_pathes_.push_back(full_path);
    }
    is_valid_ = true;
}


FSBg::FileAccessor::~FileAccessor()
{
    if (!is_valid_)
        return;
    unsigned long long size = 0;
    BOOST_FOREACH(const string& full_path, full_pathes_)
        size += GetFileSize(full_path);
    fs_bg_.total_size_ += size;
    fs_bg_.total_size_ -= initial_size_;
}


bool FSBg::FileAccessor::CheckValid() const
{
    return is_valid_;
}


const Strings& FSBg::FileAccessor::GetFullPathes() const
{
    KU_ASSERT(is_valid_);
    return full_pathes_;
}

////////////////////////////////////////////////////////////////////////////////
// ReadFileData and GetPathDepth definitions
////////////////////////////////////////////////////////////////////////////////

bool ku::ReadFileData(const std::string& path, Chars& data)
{
    int fd = open(path.c_str(), O_RDONLY);
    JS_ERRNO_CHECK(fd != -1);
    struct stat st;
    int ret = fstat(fd, &st);
    KU_ASSERT(ret == 0);
    if (S_ISDIR(st.st_mode)) {
        close(fd);
        JS_THROW(Error, "Attempt to read directory");
        return false;
    }
    KU_ASSERT(S_ISREG(st.st_mode));
    data.resize(st.st_size);
    ssize_t bytes_readen = read(fd, &data[0], st.st_size);
    KU_ASSERT(bytes_readen == st.st_size);
    close(fd);
    return true;
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
