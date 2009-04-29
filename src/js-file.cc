
// (c) 2009 by Anton Korenyushkin

/// \file js-data.cc
/// JavaScript binary data handler impl

#include "js-file.h"

#include <boost/lexical_cast.hpp>

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
    const size_t MAX_DIR_DEPTH = 30;
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
// DataBg
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
// TmpFileBg
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


auto_ptr<struct stat> FSManager::GetStat() const
{
    auto_ptr<struct stat> result(new struct stat());
    if (stat(path_.c_str(), result.get()) == -1)
        return auto_ptr<struct stat>();
    return result;
}


bool FSManager::MkDir() const
{
    JS_ERRNO_CHECK(!mkdir(path_.c_str(), 0700));
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

////////////////////////////////////////////////////////////////////////////////
// FSBg
////////////////////////////////////////////////////////////////////////////////

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
    size_t depth = 0;
    for (size_t from = 0; from < rel_path.size(); ++from) {
        size_t to = rel_path.find('/', from);
        if (to == from)
            continue;
        string component = rel_path.substr(from, to - from - 1);
        if (component == "." || component == "..") {
            JS_THROW(Error, "Path could not contain . or .. components");
            return false;
        }
        ++depth;
        if (to == string::npos)
            break;
        from = to;
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
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 1);
    string path;
    JS_CAN_THROW(ReadPath(args[0], path, false));
    JS_CAN_THROW(FSManager(path).MkDir());
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, WriteCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 2);
    string path;
    JS_CAN_THROW(ReadPath(args[0], path, false));
    DataBg* data_bg_ptr = DataBg::GetJSClass().Cast(args[1]);
    if (data_bg_ptr) {
        const Chars& data(data_bg_ptr->GetData());
        JS_CAN_THROW(FSManager(path).Write(&data[0], data.size()));
    } else {
        String::AsciiValue ascii_value(args[1]);
        JS_CAN_THROW(FSManager(path).Write(*ascii_value, ascii_value.length()));
    }
    return Undefined();
}


namespace
{
    void MarkTmpFileRemoved(Handle<v8::Value> value)
    {
        TmpFileBg* tmp_file_bg_ptr = TmpFileBg::GetJSClass().Cast(value);
        if (tmp_file_bg_ptr)
            tmp_file_bg_ptr->ClearPath();
    }
}

DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, RmCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 1);
    string path;
    JS_CAN_THROW(ReadPath(args[0], path, false) &&
                 FSManager(path).Rm());
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
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 2);
    string from_path, to_path;
    JS_CAN_THROW(ReadPath(args[0], from_path, false) &&
                 ReadPath(args[1], to_path, false) &&
                 FSManager(from_path).CopyFile(to_path));
    return Undefined();
}

////////////////////////////////////////////////////////////////////////////////
// ReadFileData
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
