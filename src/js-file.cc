
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
using boost::shared_ptr;


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

auto_ptr<Chars> ku::ReadFileData(const std::string& path)
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
    auto_ptr<Chars> result(new Chars(st.st_size));
    ssize_t bytes_readen = read(fd, &result->front(), st.st_size);
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
// BinaryBg::Reader definitions
////////////////////////////////////////////////////////////////////////////////

BinaryBg::Reader::Reader(Handle<v8::Value> value)
    : binary_ptr_(BinaryBg::GetJSClass().Cast(value))
    , utf8_value_ptr_(binary_ptr_ ? 0 : new String::Utf8Value(value))
{
}


BinaryBg::Reader::~Reader()
{
}


const char* BinaryBg::Reader::GetStartPtr() const
{
    return (binary_ptr_
            ? binary_ptr_->start_ptr_
            : utf8_value_ptr_.get()
            ? **utf8_value_ptr_
            : 0);
}


size_t BinaryBg::Reader::GetSize() const
{
    return (binary_ptr_
            ? binary_ptr_->size_
            : utf8_value_ptr_.get()
            ? utf8_value_ptr_->length()
            : 0);
}

////////////////////////////////////////////////////////////////////////////////
// BinaryBg::Holder
////////////////////////////////////////////////////////////////////////////////

class BinaryBg::Holder {
public:
    Holder(auto_ptr<Chars> data_ptr);
    ~Holder();

private:
    Chars data_;
};


BinaryBg::Holder::Holder(auto_ptr<Chars> data_ptr)
{
    swap(data_, *data_ptr);
    V8::AdjustAmountOfExternalAllocatedMemory(data_.size());
}


BinaryBg::Holder::~Holder()
{
    V8::AdjustAmountOfExternalAllocatedMemory(-data_.size());
}

////////////////////////////////////////////////////////////////////////////////
// BinaryBg
////////////////////////////////////////////////////////////////////////////////

JSClass<BinaryBg>& BinaryBg::GetJSClass()
{
    static JSClass<BinaryBg> result("Binary", ConstructorCb);
    return result;
}


void BinaryBg::AdjustTemplates(Handle<ObjectTemplate> object_template,
                               Handle<ObjectTemplate> proto_template)
{
    object_template->SetAccessor(String::NewSymbol("length"), GetLengthCb,
                                 0, Handle<v8::Value>(), DEFAULT,
                                 ReadOnly | DontEnum | DontDelete);
    SetFunction(proto_template, "_toString", ToStringCb);
    SetFunction(proto_template, "_range", RangeCb);
    SetFunction(proto_template, "_fill", FillCb);
}


namespace
{
    string ReadLower(Handle<v8::Value> value) {
        string result(Stringify(value));
        BOOST_FOREACH(char& c, result)
            c = tolower(c);
        return result;
    }


    void transcode(const char* in_ptr,
                   size_t in_length,
                   const string& from_charset,
                   const string& to_charset,
                   Chars& out_data)
    {
        iconv_t cd = iconv_open(to_charset.c_str(), from_charset.c_str());
        if (cd == reinterpret_cast<iconv_t>(-1))
            throw Error(Error::CONVERSION,
                        ("Conversion from \"" + from_charset +
                         "\" to \"" + to_charset +
                         "\" is not supported"));
        out_data.resize(in_length + in_length / 8 + 32);
        char* out_ptr = &out_data[0];
        size_t out_length = out_data.size();
        for (;;) {
            size_t ret = iconv(cd,
                               const_cast<char**>(&in_ptr), &in_length,
                               &out_ptr, &out_length);
            if (ret == MINUS_ONE && errno == E2BIG) {
                char* old_start_ptr = &out_data[0];
                size_t diff = in_length + in_length / 4 + 32;
                out_data.resize(out_data.size() + diff);
                out_length += diff;
                out_ptr = &out_data[0] + (out_ptr - old_start_ptr);
            } else {
                iconv_close(cd);
                if (ret == MINUS_ONE)
                    throw MakeErrnoError();
                out_data.resize(out_ptr - &out_data[0]);
                return;
            }
        }
    }
}


Handle<v8::Value> BinaryBg::ConstructorCb(const Arguments& args)
{
    if (!args.IsConstructCall())
        return Undefined();
    try {
        auto_ptr<Chars> data_ptr(new Chars());
        Chars& data(*data_ptr);
        if (args.Length()) {
            if (args[0]->IsInt32()) {
                int32_t size = args[0]->Int32Value();
                if (size < 0)
                    throw Error(Error::RANGE, "Length must be positive");
                data.assign(size,
                            args.Length() > 1 ? args[1]->Uint32Value() : 0);
            } else if (args[0]->IsString()) {
                Handle<String> str(args[0]->ToString());
                string charset(args.Length() > 1
                               ? ReadLower(args[1])
                               : "utf-8");
                if (charset == "utf-8" || charset == "utf8") {
                    int size = str->Utf8Length();
                    data.resize(size);
                    str->WriteUtf8(&data[0], size);
                } else {
                    String::Value utf16_value(str);
                    transcode(reinterpret_cast<const char*>(*utf16_value),
                              utf16_value.length() * 2,
                              "utf-16",
                              charset,
                              data);
                }
            } else if (args[0]->IsArray()) {
                Handle<Array> array(Handle<Array>::Cast(args[0]));
                size_t size = array->Length();
                data.resize(size);
                for (size_t i = 0; i < size; ++i)
                    data[i] = array->Get(Integer::New(i))->Uint32Value();
            } else if (const BinaryBg* binary_ptr =
                       BinaryBg::GetJSClass().Cast(args[0])) {
                if (args.Length() == 1) {
                    data.assign(binary_ptr->start_ptr_,
                                binary_ptr->start_ptr_ + binary_ptr->size_);
                } else if (const BinaryBg* second_binary_ptr =
                           BinaryBg::GetJSClass().Cast(args[1])) {
                    vector<const BinaryBg*> binary_ptrs(args.Length());
                    binary_ptrs[0] = binary_ptr;
                    binary_ptrs[1] = second_binary_ptr;
                    size_t size = binary_ptr->size_ + second_binary_ptr->size_;
                    for (int i = 2; i < args.Length(); ++i) {
                        binary_ptr = BinaryBg::GetJSClass().Cast(args[i]);
                        if (!binary_ptr)
                            throw Error(Error::TYPE, "Another Binary expected");
                        binary_ptrs[i] = binary_ptr;
                        size += binary_ptr->size_;
                    }
                    data.resize(size);
                    char* start_ptr = &data[0];
                    BOOST_FOREACH(binary_ptr, binary_ptrs) {
                        memcpy(start_ptr,
                               binary_ptr->start_ptr_,
                               binary_ptr->size_);
                        start_ptr += binary_ptr->size_;
                    }
                } else {
                    string to_charset(ReadLower(args[1]));
                    string from_charset(args.Length() > 2 ?
                                        ReadLower(args[2])
                                        : "utf-8");
                    transcode(binary_ptr->start_ptr_, binary_ptr->size_,
                              from_charset, to_charset,
                              data);
                }
            } else {
                throw Error(Error::TYPE,
                            "Binary, Array, string or integer required");
            }
        }
        BinaryBg* new_binary_ptr = new BinaryBg(data_ptr);
        BinaryBg::GetJSClass().Attach(args.This(), new_binary_ptr);
        args.This()->SetIndexedPropertiesToExternalArrayData(
            new_binary_ptr->start_ptr_,
            kExternalByteArray,
            new_binary_ptr->size_);
        return Handle<v8::Value>();
    } JS_CATCH(Handle<v8::Value>);
}


BinaryBg::BinaryBg(auto_ptr<Chars> data_ptr)
{
    if (!data_ptr.get() || data_ptr->empty()) {
        start_ptr_ = 0;
        size_ = 0;
    } else {
        start_ptr_ = &data_ptr->front();
        size_ = data_ptr->size();
        holder_ptr_.reset(new Holder(data_ptr));
    }
}


BinaryBg::BinaryBg(const BinaryBg& parent, size_t start, size_t stop)
{
    stop = min(stop, parent.size_);
    if (start >= stop) {
        start_ptr_ = 0;
        size_ = 0;
    } else {
        start_ptr_ = parent.start_ptr_ + start;
        size_ = stop - start;
        holder_ptr_ = parent.holder_ptr_;
    }
}


BinaryBg::~BinaryBg()
{
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, BinaryBg, GetLengthCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/) const
{
    return Integer::New(size_);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, BinaryBg, ToStringCb,
                    const Arguments&, args) const
{
    string charset(args.Length() ? Stringify(args[0]) : "utf-8");
    if (charset == "utf-8" || charset == "utf8")
        return String::New(start_ptr_, size_);
    Chars data;
    transcode(start_ptr_, size_, charset, "utf-16le", data);
    assert(data.size() % 2 == 0);
    return String::New(reinterpret_cast<uint16_t*>(&data[0]), data.size() / 2);
}


size_t BinaryBg::ReadIndex(Handle<v8::Value> value) const
{
    int32_t index = value->Int32Value();
    if (index >= 0)
        return index;
    size_t abs_index = -index;
    return size_ > abs_index ? size_ - abs_index : 0;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, BinaryBg, RangeCb,
                    const Arguments&, args) const
{
    switch (args.Length()) {
    case 0:
        return JSNew<BinaryBg>(*this);
    case 1:
        return JSNew<BinaryBg>(*this, ReadIndex(args[0]));
    default:
        return JSNew<BinaryBg>(*this, ReadIndex(args[0]), ReadIndex(args[1]));
    }
}

DEFINE_JS_CALLBACK1(Handle<v8::Value>, BinaryBg, FillCb,
                    const Arguments&, args) const
{
    memset(start_ptr_, args.Length() ? args[0]->Uint32Value() : 0, size_);
    return args.This();
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


    size_t GetFileSize(const string& path)
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


DEFINE_JS_CLASS(FSBg, "FS", object_template, /*proto_template*/)
{
    TempFileBg::GetJSClass();
    BinaryBg::GetJSClass();
    SetFunction(object_template, "read", ReadCb);
    SetFunction(object_template, "exists", ExistsCb);
    SetFunction(object_template, "isDir", IsDirCb);
    SetFunction(object_template, "isFile", IsFileCb);
    SetFunction(object_template, "getModDate", GetModDateCb);
    SetFunction(object_template, "list", ListCb);
    SetFunction(object_template, "createDir", CreateDirCb);
    SetFunction(object_template, "write", WriteCb);
    SetFunction(object_template, "remove", RemoveCb);
    SetFunction(object_template, "rename", RenameCb);
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
    return JSNew<BinaryBg>(ReadFileData(path));
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
    size_t old_size = GetFileSize(path);
    BinaryBg::Reader binary_reader(args[1]);
    size_t size = binary_reader.GetSize();
    if (size > old_size)
        CheckTotalSize(size - old_size);
    int fd = creat(path.c_str(), 0644);
    if (fd == -1)
        throw MakeErrnoError();
    ssize_t bytes_written = write(fd, binary_reader.GetStartPtr(), size);
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
    size_t size = GetFileSize(path);
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

FSBg::FileAccessor::FileAccessor(FSBg& fs_bg, Handle<Array> files)
    : fs_bg_(fs_bg)
    , initial_size_(0)
{
    full_pathes_.reserve(files->Length());
    for (size_t i = 0; i < files->Length(); ++i) {
        string full_path(fs_bg.ReadPath(files->Get(Integer::New(i)), false));
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
