
// (c) 2009-2010 by Anton Korenyushkin

#include "js-file.h"
#include "db.h"

#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>
#include <openssl/md5.h>
#include <openssl/sha.h>

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

auto_ptr<Chars> ku::ReadFile(const std::string& path)
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

DEFINE_JS_CONSTRUCTOR(BinaryBg, "Binary", ConstructorCb,
                      object_template, proto_template)
{
    object_template->SetAccessor(String::NewSymbol("length"), GetLengthCb,
                                 0, Handle<v8::Value>(), DEFAULT,
                                 ReadOnly | DontEnum | DontDelete);
    SetFunction(proto_template, "_toString", ToStringCb);
    SetFunction(proto_template, "_range", RangeCb);
    SetFunction(proto_template, "_fill", FillCb);
    SetFunction(proto_template, "_indexOf", IndexOfCb);
    SetFunction(proto_template, "_lastIndexOf", LastIndexOfCb);
    SetFunction(proto_template, "_compare", CompareCb);
    SetFunction(proto_template, "_md5", Md5Cb);
    SetFunction(proto_template, "_sha1", Sha1Cb);
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
                    data.assign(binary_ptr->data_,
                                binary_ptr->data_ + binary_ptr->size_);
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
                               binary_ptr->data_,
                               binary_ptr->size_);
                        start_ptr += binary_ptr->size_;
                    }
                } else {
                    string to_charset(ReadLower(args[1]));
                    string from_charset(args.Length() > 2 ?
                                        ReadLower(args[2])
                                        : "utf-8");
                    transcode(binary_ptr->data_, binary_ptr->size_,
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
        new_binary_ptr->SetIndexedProperties(args.This());
        return Handle<v8::Value>();
    } JS_CATCH(Handle<v8::Value>);
}


BinaryBg::BinaryBg(auto_ptr<Chars> data_ptr)
{
    if (!data_ptr.get() || data_ptr->empty()) {
        data_ = 0;
        size_ = 0;
    } else {
        data_ = &data_ptr->front();
        size_ = data_ptr->size();
        holder_ptr_.reset(new Holder(data_ptr));
    }
}


BinaryBg::BinaryBg(const BinaryBg& parent, size_t start, size_t stop)
{
    stop = min(stop, parent.size_);
    if (start >= stop) {
        data_ = 0;
        size_ = 0;
    } else {
        data_ = parent.data_ + start;
        size_ = stop - start;
        holder_ptr_ = parent.holder_ptr_;
    }
}


BinaryBg::~BinaryBg()
{
}


const char* BinaryBg::GetData() const
{
    return data_;
}


size_t BinaryBg::GetSize() const
{
    return size_;
}


void BinaryBg::SetIndexedProperties(Handle<Object> object) const
{
    object->SetIndexedPropertiesToExternalArrayData(
        data_, kExternalUnsignedByteArray, size_);
}


Handle<Object> BinaryBg::Wrap()
{
    Handle<Object> result(GetJSClass().Instantiate(this));
    SetIndexedProperties(result);
    return result;
}


Handle<Object> BinaryBg::New(auto_ptr<Chars> data_ptr)
{
    return (new BinaryBg(data_ptr))->Wrap();
}


v8::Handle<v8::Object> BinaryBg::New(const BinaryBg& parent,
                                     size_t start,
                                     size_t stop)
{
    return (new BinaryBg(parent, start, stop))->Wrap();
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
        return String::New(data_, size_);
    Chars data;
    transcode(data_, size_, charset, "utf-16le", data);
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
    BinaryBg* binary_ptr =
        args.Length() == 0
        ? new BinaryBg(*this)
        : args.Length() == 1
        ? new BinaryBg(*this, ReadIndex(args[0]))
        : new BinaryBg(*this, ReadIndex(args[0]), ReadIndex(args[1]));
    return binary_ptr->Wrap();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, BinaryBg, FillCb,
                    const Arguments&, args) const
{
    memset(data_, args.Length() ? args[0]->Uint32Value() : 0, size_);
    return args.This();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, BinaryBg, IndexOfCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    Binarizator binarizator(args[0]);
    size_t index = args.Length() > 1 ? ReadIndex(args[1]) : 0;
    if (!binarizator.GetSize())
        return Integer::New(min(index, size_));
    if (index >= size_)
        return Integer::New(-1);
    const char* end = data_ + size_;
    const char* found = search(const_cast<const char*>(data_ + index),
                               end,
                               binarizator.GetData(),
                               binarizator.GetData() + binarizator.GetSize());
    return Integer::New(found == end
                        ? -1
                        : found - data_);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, BinaryBg, LastIndexOfCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    Binarizator binarizator(args[0]);
    size_t index = args.Length() > 1 ? ReadIndex(args[1]) : size_;
    if (!binarizator.GetSize())
        return Integer::New(min(index, size_));
    const char* end = data_ + min(index + binarizator.GetSize(), size_);
    const char* found = find_end(const_cast<const char*>(data_),
                                 end,
                                 binarizator.GetData(),
                                 binarizator.GetData() + binarizator.GetSize());
    return Integer::New(found == end
                        ? -1
                        : found - data_);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, BinaryBg, CompareCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    const BinaryBg* other_ptr = BinaryBg::GetJSClass().Cast(args[0]);
    if (!other_ptr)
        throw Error(Error::TYPE, "Binary expected");
    int cmp = memcmp(data_,
                     other_ptr->data_,
                     min(size_, other_ptr->size_));
    return Integer::New(cmp == 0
                        ? (size_ == other_ptr->size_
                           ? 0
                           : (size_ > other_ptr->size_ ? 1 : -1))
                        : (cmp > 0 ? 1 : -1));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, BinaryBg, Md5Cb,
                    const Arguments&, /*args*/) const
{
    auto_ptr<Chars> data_ptr(new Chars(16));
    MD5(reinterpret_cast<unsigned char*>(data_),
        size_,
        reinterpret_cast<unsigned char*>(&data_ptr->front()));
    return BinaryBg::New(data_ptr);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, BinaryBg, Sha1Cb,
                    const Arguments&, /*args*/) const
{
    auto_ptr<Chars> data_ptr(new Chars(20));
    SHA1(reinterpret_cast<unsigned char*>(data_),
         size_,
         reinterpret_cast<unsigned char*>(&data_ptr->front()));
    return BinaryBg::New(data_ptr);
}

////////////////////////////////////////////////////////////////////////////////
// Binarizator definitions
////////////////////////////////////////////////////////////////////////////////

Binarizator::Binarizator(Handle<v8::Value> value)
    : binary_ptr_(BinaryBg::GetJSClass().Cast(value))
    , utf8_value_ptr_(binary_ptr_ ? 0 : new String::Utf8Value(value))
{
}


Binarizator::~Binarizator()
{
}


const char* Binarizator::GetData() const
{
    return (binary_ptr_
            ? binary_ptr_->GetData()
            : utf8_value_ptr_.get()
            ? **utf8_value_ptr_
            : 0);
}


size_t Binarizator::GetSize() const
{
    return (binary_ptr_
            ? binary_ptr_->GetSize()
            : utf8_value_ptr_.get()
            ? utf8_value_ptr_->length()
            : 0);
}

////////////////////////////////////////////////////////////////////////////////
// FSQuotaChecker definitions
////////////////////////////////////////////////////////////////////////////////

class ku::FSQuotaChecker {
public:
    FSQuotaChecker(uint64_t quota, uint64_t size);
    void Check() const;
    void Change(int64_t diff);

private:
    uint64_t quota_;
    uint64_t size_;
};


FSQuotaChecker::FSQuotaChecker(uint64_t quota, uint64_t size)
    : quota_(quota)
    , size_(size)
{
}


void FSQuotaChecker::Check() const
{
    if (size_ > quota_)
        throw Error(Error::FS_QUOTA, "File storage quota exceeded");
}


void FSQuotaChecker::Change(int64_t diff)
{
    size_ = max(0LL, static_cast<int64_t>(size_) + diff);
}

////////////////////////////////////////////////////////////////////////////////
// FileBg::ChangeScope
////////////////////////////////////////////////////////////////////////////////

class FileBg::ChangeScope {
public:
    ChangeScope(const FileBg& file);
    ~ChangeScope();

private:
    const FileBg& file_;
    int64_t initial_size_;

    int64_t GetAllocatedSize() const;
};


FileBg::ChangeScope::ChangeScope(const FileBg& file)
    : file_(file)
{
    file.CheckOpen();
    if (!file.quota_checker_ptr_)
        throw Error(Error::FILE_IS_READ_ONLY, "File is read only");
    file.quota_checker_ptr_->Check();
    initial_size_ = GetAllocatedSize();
}


FileBg::ChangeScope::~ChangeScope()
{
    file_.quota_checker_ptr_->Change(GetAllocatedSize() - initial_size_);
}


int64_t FileBg::ChangeScope::GetAllocatedSize() const
{
    struct stat st;
    int ret = fstat(file_.fd_, &st);
    KU_ASSERT_EQUAL(ret, 0);
    return st.st_blocks * 512;
}

////////////////////////////////////////////////////////////////////////////////
// FileBg definitions
////////////////////////////////////////////////////////////////////////////////

DEFINE_JS_CLASS(FileBg, "File", object_template, proto_template)
{
    object_template->SetAccessor(String::NewSymbol("length"),
                                 GetLengthCb, SetLengthCb,
                                 Handle<v8::Value>(), DEFAULT,
                                 DontDelete);
    object_template->SetAccessor(String::NewSymbol("position"),
                                 GetPositionCb, SetPositionCb,
                                 Handle<v8::Value>(), DEFAULT,
                                 DontDelete);
    object_template->SetAccessor(String::NewSymbol("writable"),
                                 GetWritableCb, 0,
                                 Handle<v8::Value>(), DEFAULT,
                                 ReadOnly | DontDelete);
    object_template->SetAccessor(String::NewSymbol("closed"),
                                 GetClosedCb, 0,
                                 Handle<v8::Value>(), DEFAULT,
                                 ReadOnly | DontDelete);
    SetFunction(proto_template, "_close", CloseCb);
    SetFunction(proto_template, "_flush", FlushCb);
    SetFunction(proto_template, "_read", ReadCb);
    SetFunction(proto_template, "_write", WriteCb);
}


FileBg::FileBg(const std::string& path, FSQuotaChecker* quota_checker_ptr)
    : path_(path)
    , fd_(quota_checker_ptr
          ? open(path.c_str(), O_CLOEXEC | O_RDWR | O_CREAT, S_IRUSR | S_IWUSR)
          : open(path.c_str(), O_CLOEXEC | O_RDONLY))
    , quota_checker_ptr_(quota_checker_ptr)
{
    if (fd_ == -1)
        throw MakeErrnoError();
}


FileBg::~FileBg()
{
    Close();
}


string FileBg::GetPath() const
{
    return path_;
}


void FileBg::Close()
{
    if (fd_ != -1) {
        int ret = close(fd_);
        KU_ASSERT_EQUAL(ret, 0);
        fd_ = -1;
    }
}


void FileBg::CheckOpen() const
{
    if (fd_ == -1)
        throw Error(Error::VALUE, "File is already closed");
}


size_t FileBg::GetSize() const
{
    struct stat st;
    int ret = fstat(fd_, &st);
    KU_ASSERT_EQUAL(ret, 0);
    return st.st_size;
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
    ChangeScope change_scope(*this);
    int ret = ftruncate(fd_, value->Uint32Value());
    KU_ASSERT_EQUAL(ret, 0);
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, FileBg, GetPositionCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/) const
{
    CheckOpen();
    off_t position = lseek(fd_, 0, SEEK_CUR);
    KU_ASSERT(position != -1);
    return Integer::New(position);
}


DEFINE_JS_CALLBACK3(void, FileBg, SetPositionCb,
                    Local<String>, /*property*/,
                    Local<v8::Value>, value,
                    const AccessorInfo&, /*info*/) const
{
    CheckOpen();
    off_t position = lseek(fd_, value->Uint32Value(), SEEK_SET);
    KU_ASSERT(position != -1);
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, FileBg, GetWritableCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/) const
{
    CheckOpen();
    return Boolean::New(quota_checker_ptr_);
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, FileBg, GetClosedCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/) const
{
    return Boolean::New(fd_ == -1);
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
    KU_ASSERT_EQUAL(ret, 0);
    return args.This();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileBg, ReadCb,
                    const Arguments&, args) const
{
    CheckOpen();
    auto_ptr<Chars> data_ptr(
        new Chars(args.Length() ? args[0]->Uint32Value() : GetSize()));
    ssize_t count = read(fd_, &data_ptr->front(), data_ptr->size());
    KU_ASSERT(count != -1);
    data_ptr->resize(count);
    return BinaryBg::New(data_ptr);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FileBg, WriteCb,
                    const Arguments&, args) const
{
    ChangeScope change_scope(*this);
    CheckArgsLength(args, 1);
    Binarizator binarizator(args[0]);
    ssize_t count = write(fd_, binarizator.GetData(), binarizator.GetSize());
    KU_ASSERT_EQUAL(count, static_cast<ssize_t>(binarizator.GetSize()));
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


    string DoReadPath(const string& root_path,
                      Handle<v8::Value> value,
                      bool can_be_root = true)
    {
        string rel_path(Stringify(value));
        int depth = GetPathDepth(rel_path);
        if (depth < 0)
            throw Error(Error::PATH,
                        ("Path \"" + rel_path +
                         "\" leads beyond the root directory"));
        if (!can_be_root && !depth)
            throw Error(Error::PATH, "Path \"" + rel_path + "\" is empty");
        return root_path + '/' + rel_path;
    }
}


DEFINE_JS_CLASS(FSBg, "FS", object_template, /*proto_template*/)
{
    BinaryBg::GetJSClass();
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


FSBg::FSBg(const string& app_path,
           const string& release_path,
           uint64_t quota)
    : app_path_(app_path)
    , release_path_(release_path)
    , quota_checker_ptr_(new FSQuotaChecker(quota, CalcTotalSize(app_path)))
{
}


FSBg::~FSBg()
{
}


string FSBg::ReadPath(Handle<v8::Value> value, bool can_be_root) const
{
    return DoReadPath(app_path_, value, can_be_root);
}


string FSBg::ReadPath(const Arguments& args) const
{
    CheckArgsLength(args, 1);
    if (args.Length() == 1)
        return ReadPath(args[0]);
    string app_name(Stringify(args[0]));
    access_ptr->CheckAppExists(app_name);
    return DoReadPath(release_path_ + '/' + app_name, args[1]);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, OpenCb,
                    const Arguments&, args) const
{
    return JSNew<FileBg>(ReadPath(args),
                         args.Length() == 1 ? quota_checker_ptr_.get() : 0);
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
    quota_checker_ptr_->Check();
    if (mkdir(ReadPath(args[0]).c_str(), 0755) == -1)
        throw MakeErrnoError();
    quota_checker_ptr_->Change(DIRECTORY_SIZE);
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, FSBg, RemoveCb,
                    const Arguments&, args)
{
    CheckArgsLength(args, 1);
    string path(ReadPath(args[0], false));
    int64_t size = GetStat(path)->st_blocks * 512;
    if (remove(path.c_str()) == -1)
        throw MakeErrnoError();
    quota_checker_ptr_->Change(-size);
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
