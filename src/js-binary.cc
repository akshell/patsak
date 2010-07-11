
// (c) 2010 by Anton Korenyushkin

#include "js-binary.h"
#include "js-common.h"

#include <openssl/md5.h>
#include <openssl/sha.h>

#include <iconv.h>
#include <errno.h>


using namespace ak;
using namespace v8;
using namespace std;


////////////////////////////////////////////////////////////////////////////////
// BinaryHolder
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class BinaryHolder {
    public:
        BinaryHolder(auto_ptr<Chars> data_ptr);
        ~BinaryHolder();

    private:
        Chars data_;
    };
}


BinaryHolder::BinaryHolder(auto_ptr<Chars> data_ptr)
{
    swap(data_, *data_ptr);
    V8::AdjustAmountOfExternalAllocatedMemory(data_.size());
}


BinaryHolder::~BinaryHolder()
{
    V8::AdjustAmountOfExternalAllocatedMemory(-data_.size());
}

////////////////////////////////////////////////////////////////////////////////
// Utils
////////////////////////////////////////////////////////////////////////////////

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
                    throw Error(Error::CONVERSION, strerror(errno));
                out_data.resize(out_ptr - &out_data[0]);
                return;
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// BinaryBg
////////////////////////////////////////////////////////////////////////////////

class ak::BinaryBg {
public:
    DECLARE_JS_CONSTRUCTOR(BinaryBg);

    BinaryBg(auto_ptr<Chars> data_ptr = auto_ptr<Chars>());
    BinaryBg(BinaryBg& parent, size_t start = 0, size_t stop = MINUS_ONE);
    ~BinaryBg();

    Handle<Object> Wrap();

    const char* GetData() const;
    size_t GetSize() const;

private:
    boost::shared_ptr<BinaryHolder> holder_ptr_;
    char* data_;
    size_t size_;

    void SetIndexedProperties(Handle<Object> object) const;
    size_t ReadIndex(Handle<v8::Value> value) const;

    DECLARE_JS_CALLBACK2(Handle<v8::Value>, GetLengthCb,
                         Local<String>,
                         const AccessorInfo&) const;

    DECLARE_JS_CALLBACK1(Handle<v8::Value>, ToStringCb,
                         const Arguments&) const;

    DECLARE_JS_CALLBACK1(Handle<v8::Value>, RangeCb,
                         const Arguments&);

    DECLARE_JS_CALLBACK1(Handle<v8::Value>, FillCb,
                         const Arguments&);

    DECLARE_JS_CALLBACK1(Handle<v8::Value>, IndexOfCb,
                         const Arguments&) const;

    DECLARE_JS_CALLBACK1(Handle<v8::Value>, LastIndexOfCb,
                         const Arguments&) const;

    DECLARE_JS_CALLBACK1(Handle<v8::Value>, CompareCb,
                         const Arguments&) const;

    DECLARE_JS_CALLBACK1(Handle<v8::Value>, Md5Cb,
                         const Arguments&) const;

    DECLARE_JS_CALLBACK1(Handle<v8::Value>, Sha1Cb,
                         const Arguments&) const;
};


DEFINE_JS_CONSTRUCTOR(BinaryBg, "Binary", object_template, proto_template)
{
    object_template->SetAccessor(String::NewSymbol("length"), GetLengthCb,
                                 0, Handle<v8::Value>(), DEFAULT,
                                 ReadOnly | DontEnum | DontDelete);
    SetFunction(proto_template, "toString", ToStringCb);
    SetFunction(proto_template, "range", RangeCb);
    SetFunction(proto_template, "fill", FillCb);
    SetFunction(proto_template, "indexOf", IndexOfCb);
    SetFunction(proto_template, "lastIndexOf", LastIndexOfCb);
    SetFunction(proto_template, "compare", CompareCb);
    SetFunction(proto_template, "md5", Md5Cb);
    SetFunction(proto_template, "sha1", Sha1Cb);
}


DEFINE_JS_CONSTRUCTOR_CALLBACK(BinaryBg, args)
{
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
    BinaryBg* result = new BinaryBg(data_ptr);
    result->SetIndexedProperties(args.This());
    return result;
}


BinaryBg::BinaryBg(auto_ptr<Chars> data_ptr)
{
    if (!data_ptr.get() || data_ptr->empty()) {
        data_ = 0;
        size_ = 0;
    } else {
        data_ = &data_ptr->front();
        size_ = data_ptr->size();
        holder_ptr_.reset(new BinaryHolder(data_ptr));
    }
}


BinaryBg::BinaryBg(BinaryBg& parent, size_t start, size_t stop)
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
                    const Arguments&, args)
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
                    const Arguments&, args)
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
    const BinaryBg& other(GetBg<BinaryBg>(args[0]));
    int cmp = memcmp(data_, other.data_, min(size_, other.size_));
    return Integer::New(cmp == 0
                        ? (size_ == other.size_
                           ? 0
                           : (size_ > other.size_ ? 1 : -1))
                        : (cmp > 0 ? 1 : -1));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, BinaryBg, Md5Cb,
                    const Arguments&, /*args*/) const
{
    auto_ptr<Chars> data_ptr(new Chars(16));
    MD5(reinterpret_cast<unsigned char*>(data_),
        size_,
        reinterpret_cast<unsigned char*>(&data_ptr->front()));
    return NewBinary(data_ptr);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, BinaryBg, Sha1Cb,
                    const Arguments&, /*args*/) const
{
    auto_ptr<Chars> data_ptr(new Chars(20));
    SHA1(reinterpret_cast<unsigned char*>(data_),
         size_,
         reinterpret_cast<unsigned char*>(&data_ptr->front()));
    return NewBinary(data_ptr);
}

////////////////////////////////////////////////////////////////////////////////
// CastToBinary and NewBinary
////////////////////////////////////////////////////////////////////////////////

BinaryBg* ak::CastToBinary(Handle<v8::Value> value)
{
    return BinaryBg::GetJSClass().Cast(value);
}


Handle<Object> ak::NewBinary(auto_ptr<Chars> data_ptr)
{
    return (new BinaryBg(data_ptr))->Wrap();
}


Handle<Object> ak::NewBinary(BinaryBg& parent, size_t start, size_t stop)
{
    return (new BinaryBg(parent, start, stop))->Wrap();
}

////////////////////////////////////////////////////////////////////////////////
// Binarizator definitions
////////////////////////////////////////////////////////////////////////////////

Binarizator::Binarizator(Handle<v8::Value> value)
    : binary_ptr_(BinaryBg::GetJSClass().Cast(value))
    , utf8_value_ptr_(binary_ptr_ ? 0 : new String::Utf8Value(value))
{
}


Binarizator::Binarizator(const BinaryBg& binary)
    : binary_ptr_(&binary)
    , utf8_value_ptr_(0)
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
// InitBinary
////////////////////////////////////////////////////////////////////////////////

Handle<Object> ak::InitBinary()
{
    Handle<Object> result(Object::New());
    PutClass<BinaryBg>(result);
    return result;
}
