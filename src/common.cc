
// (c) 2008-2010 by Anton Korenyushkin

#include "common.h"

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include "boost/date_time/posix_time/posix_time.hpp"

#include <limits>


using namespace std;
using namespace ak;
using boost::lexical_cast;
using namespace boost::posix_time;
using namespace boost::gregorian;


////////////////////////////////////////////////////////////////////////////////
// Type
////////////////////////////////////////////////////////////////////////////////

Type::Type(const string& pg_name)
{
    if (pg_name == "float8") {
        tag_ = NUMBER;
    } else if (pg_name == "int4") {
        tag_ = INT;
    } else if (pg_name == "text") {
        tag_ = STRING;
    } else if (pg_name == "bool") {
        tag_ = BOOL;
    } else if (pg_name == "timestamp") {
        tag_ = DATE;
    } else {
        AK_ASSERT_EQUAL(pg_name, "json");
        tag_ = JSON;
    }
}


string Type::GetPgName() const
{
    static const char* pg_names[] =
        {"float8", "int4", "int4", "text", "bool", "timestamp(3)", "ak.json"};
    AK_ASSERT(tag_ < DUMMY);
    return pg_names[tag_];
}


string Type::GetName() const
{
    static const char* names[] =
        {"number", "int", "serial", "string", "bool", "date", "json"};
    AK_ASSERT(tag_ < DUMMY);
    return names[tag_];
}


string Type::GetCastFunc(Type from) const
{
    if (tag_ == from.tag_ || (IsNumeric() && from.IsNumeric()))
        return "";
    if (tag_ == DATE || tag_ == JSON)
        throw Error(Error::TYPE, "Cannot coerce any type to " + GetName());
    if  (IsNumeric())
        return "ak.to_number";
    return "ak.to_" + GetName();
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
    if (IsLogical(tag_))
        return Type::BOOL;
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
        return Type::BOOL;
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
        return Type::BOOL;
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
    virtual string GetPgLiter() const = 0;
    virtual bool Get(double& d, string& s) const = 0;
};


Value::Impl::~Impl()
{
}


namespace
{
    QuoteCallback quote_cb = 0;


    class NumberValue : public Value::Impl {
    public:
        NumberValue(double repr)
            : repr_(repr) {}

        virtual Type GetType() const {
            return Type::NUMBER;
        }

        virtual string GetPgLiter() const {
            return (repr_ != repr_
                    ? "'NaN'::float8"
                    : repr_ == numeric_limits<double>::infinity()
                    ? "'Infinity'::float8"
                    : repr_ == -numeric_limits<double>::infinity()
                    ? "'-Infinity'::float8"
                    : lexical_cast<string>(repr_));
        }

        virtual bool Get(double& d, string& /*s*/) const {
            d = repr_;
            return false;
        }

    private:
        double repr_;
    };


    class StringValue : public Value::Impl {
    public:
        StringValue(const string& repr)
            : repr_(repr) {}

        virtual Type GetType() const {
            return Type::STRING;
        }

        virtual string GetPgLiter() const {
            return quote_cb(repr_);
        }

        virtual bool Get(double& /*d*/, string& s) const {
            s = repr_;
            return true;
        }

    private:
        string repr_;
    };


    class BooleanValue : public Value::Impl {
    public:
        BooleanValue(bool repr)
            : repr_(repr) {}

        virtual Type GetType() const {
            return Type::BOOL;
        }

        virtual string GetPgLiter() const {
            return repr_ ? "true" : "false";
        }

        virtual bool Get(double& d, string& /*s*/) const {
            d = repr_;
            return false;
        }

    private:
        bool repr_;
    };


    class DateValue : public Value::Impl {
    public:
        static const ptime& GetEpoch() {
            static const ptime epoch(date(1970,1,1));
            return epoch;
        }

        DateValue(const ptime& repr)
            : repr_(repr) {}

        virtual Type GetType() const {
            return Type::DATE;
        }

        virtual string GetPgLiter() const {
            ostringstream oss;
            oss << '\'' << repr_ << "'::" + GetType().GetPgName();
            return oss.str();
        }

        virtual bool Get(double& d, string& /*s*/) const {
            d = (repr_ - GetEpoch()).total_milliseconds();
            return false;
        }

    private:
        ptime repr_;

        static const locale& GetLocale() {
            static const locale
                loc(locale::classic(),
                    new time_facet("%a, %d %b %Y %H:%M:%S GMT"));
            return loc;
        }
    };


    class JSONValue : public StringValue {
    public:
        JSONValue(const string& repr)
            : StringValue(repr) {}

        virtual Type GetType() const {
            return Type::JSON;
        }
    };
}


namespace
{
    Value::Impl* CreateValueImplByDouble(Type type, double d)
    {
        if (type.IsNumeric()) {
            return new NumberValue(d);
        } else {
            AK_ASSERT(type == Type::DATE);
            if (d != d)
                throw Error(Error::TYPE, "Invalid date");
            return new DateValue(DateValue::GetEpoch() + milliseconds(d));
        }
    }


    Value::Impl* CreateValueImplByString(Type type, const string& s)
    {
        if (type == Type::STRING) {
            return new StringValue(s);
        } else if (type.IsNumeric()) {
            return new NumberValue(
                s.substr(0, 5) == "'NaN'"
                ? numeric_limits<double>::quiet_NaN()
                : s[0] == '('
                ? lexical_cast<double>(s.substr(1, s.size() - 2))
                : lexical_cast<double>(s));
        } else if (type == Type::BOOL) {
            AK_ASSERT(s == "true" || s == "false");
            return new BooleanValue(s == "true");
        } else if (type == Type::DATE) {
            return new DateValue(time_from_string(s));
        } else {
            AK_ASSERT(type == Type::JSON);
            return new JSONValue(s);
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
    AK_ASSERT(type == Type::BOOL);
    pimpl_.reset(new BooleanValue(b));
}


Value::~Value()
{
}


Type Value::GetType() const
{
    return pimpl_->GetType();
}


string Value::GetPgLiter() const
{
    return pimpl_->GetPgLiter();
}


bool Value::Get(double& d, string& s) const
{
    return pimpl_->Get(d, s);
}

////////////////////////////////////////////////////////////////////////////////
// InitCommon
////////////////////////////////////////////////////////////////////////////////

void ak::InitCommon(QuoteCallback quote_cb)
{
    ::quote_cb = quote_cb;
}
