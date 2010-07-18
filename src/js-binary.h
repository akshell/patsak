// (c) 2010 by Anton Korenyushkin

#ifndef JS_BINARY_H
#define JS_BINARY_H

#include "common.h"

#include <v8.h>

#include <memory>


namespace ak
{
    class BinaryBg;

    BinaryBg* CastToBinary(v8::Handle<v8::Value> value);

    v8::Handle<v8::Object> NewBinary(std::auto_ptr<Chars> data_ptr =
                                     std::auto_ptr<Chars>());

    v8::Handle<v8::Object> NewBinary(BinaryBg& parent,
                                     size_t start = 0,
                                     size_t stop = MINUS_ONE);


    class Binarizator {
    public:
        Binarizator(v8::Handle<v8::Value> value);
        Binarizator(const BinaryBg& binary);
        ~Binarizator();
        const char* GetData() const;
        size_t GetSize() const;

    private:
        const BinaryBg* binary_ptr_;
        std::auto_ptr<v8::String::Utf8Value> utf8_value_ptr_;
    };


    v8::Handle<v8::Object> InitBinary();
}

#endif // JS_BINARY_H
