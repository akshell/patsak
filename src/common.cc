// (c) 2008-2010 by Anton Korenyushkin

#include "common.h"

#include <boost/lexical_cast.hpp>

#include <time.h>


using namespace std;
using namespace ak;
using boost::lexical_cast;


////////////////////////////////////////////////////////////////////////////////
// Type
////////////////////////////////////////////////////////////////////////////////

string Type::GetPgName() const
{
    static const char* pg_names[] = {
        "float8",
        "int4",
        "int4",
        "text",
        "bool",
        "timestamp(3)",
        "ak.json",
        "bytea"
    };
    AK_ASSERT(tag_ < DUMMY);
    return pg_names[tag_];
}


string Type::GetName() const
{
    static const char* names[] = {
        "number",
        "integer",
        "serial",
        "string",
        "boolean",
        "date",
        "json",
        "binary"
    };
    AK_ASSERT(tag_ < DUMMY);
    return names[tag_];
}


string Type::GetCastFunc(Type from) const
{
    if (tag_ == from.tag_ || (IsNumeric() && from.IsNumeric()))
        return "";
    if (tag_ == DATE || tag_ == JSON)
        throw Error(Error::TYPE, "Cannot coerce any type to " + GetName());
    if (from == BINARY && tag_ != BOOLEAN)
        throw Error(Error::TYPE, "Cannot coerce binary to " + GetName());
    if (IsNumeric())
        return "ak.to_number";
    return "ak.to_" + GetName();
}


Type ak::ReadType(const string& name)
{
    if (name == "number")
        return Type::NUMBER;
    if (name == "integer")
        return Type::INTEGER;
    if (name == "serial")
        return Type::SERIAL;
    if (name == "string")
        return Type::STRING;
    if (name == "boolean")
        return Type::BOOLEAN;
    if (name == "date")
        return Type::DATE;
    if (name == "json")
        return Type::JSON;
    if (name == "binary")
        return Type::BINARY;
    throw Error(Error::VALUE, "Type " + name + " doesn't exist");
}


Type ak::ReadPgType(const string& pg_name)
{
    if (pg_name == "float8")
        return Type::NUMBER;
    if (pg_name == "int4")
        return Type::INTEGER;
    if (pg_name == "text")
        return Type::STRING;
    if (pg_name == "bool")
        return Type::BOOLEAN;
    if (pg_name == "timestamp")
        return Type::DATE;
    if (pg_name == "json")
        return Type::JSON;
    AK_ASSERT_EQUAL(pg_name, "bytea");
    return Type::BINARY;
}

////////////////////////////////////////////////////////////////////////////////
// BinaryOp
////////////////////////////////////////////////////////////////////////////////

namespace
{
    bool IsLogical(BinaryOp::Tag tag)
    {
        return tag == BinaryOp::LOG_AND || tag == BinaryOp::LOG_OR;
    }


    bool IsComparison(BinaryOp::Tag tag)
    {
        switch (tag) {
        case BinaryOp::LT:
        case BinaryOp::GT:
        case BinaryOp::LE:
        case BinaryOp::GE:
        case BinaryOp::EQ:
        case BinaryOp::NE:
            return true;
        default:
            return false;
        }
    }
}

string BinaryOp::GetName() const
{
    static const char* names[] = {
        "+", // SUM,
        "-", // SUB
        "*", // MUL
        "/", // DIV
        "%", // MOD
        "<", // LT
        ">", // GT
        "<=", // LE
        ">=", // GE
        "==", // EQ
        "!=", // NE
        "&&", // LOG_AND
        "||"  // LOG_OR
    };
    return names[tag_];
}


Type BinaryOp::GetCommonType(Type left_type, Type right_type) const
{
    if (left_type == Type::BINARY || right_type == Type::BINARY)
        throw Error(Error::TYPE, "Operation cannot be applied to binary");
    if (IsLogical(tag_))
        return Type::BOOLEAN;
    if (IsComparison(tag_)) {
        if (left_type == right_type)
            return left_type;
        if ((left_type == Type::JSON && right_type == Type::STRING) ||
            (left_type == Type::STRING && right_type == Type::JSON))
            return Type::STRING;
        return Type::NUMBER;
    }
    if (tag_ == SUM &&
        (left_type == Type::STRING || right_type == Type::STRING))
        return Type::STRING;
    return Type::NUMBER;
}


Type BinaryOp::GetResultType(Type common_type) const
{
    if (IsComparison(tag_))
        return Type::BOOLEAN;
    return common_type;
}


string BinaryOp::GetPgName(Type common_type) const
{
    static const char* pg_names[] = {
        "+",   // SUM,
        "-",   // SUB
        "*",   // MUL
        "/",   // DIV
        "%",   // MOD
        "<",   // LT
        ">",   // GT
        "<=",  // LE
        ">=",  // GE
        "=",   // EQ
        "<>",  // NE
        "AND", // LOG_AND
        "OR"   // LOG_OR
    };
    if (tag_ == SUM && common_type == Type::STRING)
        return "||";
    return pg_names[tag_];
}

////////////////////////////////////////////////////////////////////////////////
// UnaryOp
////////////////////////////////////////////////////////////////////////////////

string UnaryOp::GetName() const
{
    static const char* names[] = {
        "+", // PLUS
        "-", // MINUS
        "!"  // NEG
    };

    return names[tag_];
}


Type UnaryOp::GetOpType() const
{
    switch (tag_) {
    case PLUS:
    case MINUS:
        return Type::NUMBER;
    default:
        AK_ASSERT_EQUAL(tag_, NEG);
        return Type::BOOLEAN;
    }
}


string UnaryOp::GetPgName() const
{
    switch (tag_) {
    case PLUS:
        return "+";
    case MINUS:
        return "-";
    default:
        AK_ASSERT_EQUAL(tag_, NEG);
        return "NOT";
    }
}

////////////////////////////////////////////////////////////////////////////////
// Value
////////////////////////////////////////////////////////////////////////////////

class Value::Impl {
public:
    virtual ~Impl();
    virtual Type GetType() const = 0;
    virtual bool Get(double& d, string& s) const = 0;
    virtual void Print(ostream& os) const = 0;
};


Value::Impl::~Impl()
{
}


namespace
{
    EscapeCallback escape_cb = 0;


    class NumberValue : public Value::Impl {
    public:
        NumberValue(double repr)
            : repr_(repr) {}

        virtual Type GetType() const {
            return Type::NUMBER;
        }

        virtual bool Get(double& d, string& /*s*/) const {
            d = repr_;
            return false;
        }

        virtual void Print(ostream& os) const {
            os << (repr_ != repr_
                   ? "'NaN'::float8"
                   : repr_ == numeric_limits<double>::infinity()
                   ? "'Infinity'::float8"
                   : repr_ == -numeric_limits<double>::infinity()
                   ? "'-Infinity'::float8"
                   : lexical_cast<string>(repr_));
        }

    private:
        double repr_;
    };


    class StringValue : public Value::Impl {
    public:
        StringValue(Type type, const string& repr)
            : type_(type), repr_(repr) {}

        virtual Type GetType() const {
            return type_;
        }

        virtual bool Get(double& /*d*/, string& s) const {
            s = repr_;
            return true;
        }

        virtual void Print(ostream& os) const {
            os << "'" << escape_cb(repr_, type_ == Type::BINARY) << "'";
        }

    private:
        Type type_;
        string repr_;
    };


    class BooleanValue : public Value::Impl {
    public:
        BooleanValue(bool repr)
            : repr_(repr) {}

        virtual Type GetType() const {
            return Type::BOOLEAN;
        }

        virtual bool Get(double& d, string& /*s*/) const {
            d = repr_;
            return false;
        }

        virtual void Print(ostream& os) const {
            os << (repr_ ? "true" : "false");
        }


    private:
        bool repr_;
    };


    class DateValue : public Value::Impl {
    public:
        DateValue(double d) {
            time_t t = d / 1000;
            localtime_r(&t, &tm_);
            ms_ = d - static_cast<double>(t) * 1000;
        }

        DateValue(const string& s) {
            char* rest = strptime(s.c_str(), "%F %T", &tm_);
            AK_ASSERT(rest);
            tm_.tm_isdst = -1;
            ms_ = 0;
            if (*rest == '.') {
                ++rest;
                for (size_t m = 100; *rest && m; ++rest, m /= 10)
                    ms_ += (*rest - '0') * m;
            }
        }

        virtual Type GetType() const {
            return Type::DATE;
        }

        virtual bool Get(double& d, string& /*s*/) const {
            d = static_cast<double>(mktime(&tm_)) * 1000 + ms_;
            return false;
        }

        virtual void Print(ostream& os) const {
            const size_t size = 40;
            char buf[size];
            strftime(buf, size, "'%F %T.***'::timestamp(3)", &tm_);
            size_t start  = ms_;
            for (char* ptr = buf + 23; *ptr == '*'; --ptr, start /= 10)
                *ptr = start % 10 + '0';
            os << buf;
        }


    private:
        mutable struct tm tm_; // mutable for mktime
        size_t ms_;
    };


    Value::Impl* CreateValueImplByDouble(Type type, double d)
    {
        if (type.IsNumeric()) {
            return new NumberValue(d);
        } else {
            AK_ASSERT(type == Type::DATE);
            if (d != d)
                throw Error(Error::TYPE, "Invalid date");
            return new DateValue(d);
        }
    }


    Value::Impl* CreateValueImplByString(Type type, const string& s)
    {
        if (type == Type::STRING ||
            type == Type::JSON ||
            type == Type::BINARY) {
            return new StringValue(type, s);
        } else if (type.IsNumeric()) {
            return new NumberValue(
                s.substr(0, 5) == "'NaN'"
                ? numeric_limits<double>::quiet_NaN()
                : s[0] == '('
                ? lexical_cast<double>(s.substr(1, s.size() - 2))
                : lexical_cast<double>(s));
        } else if (type == Type::BOOLEAN) {
            AK_ASSERT(s == "true" || s == "false");
            return new BooleanValue(s == "true");
        } else {
            AK_ASSERT(type == Type::DATE);
            return new DateValue(s);
        }
    }
}


Value::Value(Type type, double d)
    : pimpl_(CreateValueImplByDouble(type, d))
{
}


Value::Value(Type type, int i)
    : pimpl_(CreateValueImplByDouble(type, i))
{
}


Value::Value(Type type, const string& s)
    : pimpl_(CreateValueImplByString(type, s))
{
}


Value::Value(Type type, const char* c)
    : pimpl_(CreateValueImplByString(type, c))
{
}


Value::Value(Type type, bool b)
{
    AK_ASSERT(type == Type::BOOLEAN);
    pimpl_.reset(new BooleanValue(b));
}


Value::~Value()
{
}


Type Value::GetType() const
{
    return pimpl_->GetType();
}


bool Value::Get(double& d, string& s) const
{
    return pimpl_->Get(d, s);
}


void Value::Print(ostream& os) const
{
    pimpl_->Print(os);
}

////////////////////////////////////////////////////////////////////////////////
// InitCommon
////////////////////////////////////////////////////////////////////////////////

void ak::InitCommon(EscapeCallback escape_cb)
{
    ::escape_cb = escape_cb;
}
