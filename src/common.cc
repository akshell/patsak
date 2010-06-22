
// (c) 2008-2010 by Anton Korenyushkin

#include "error.h"
#include "utils.h"

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include "boost/date_time/posix_time/posix_time.hpp"

#include <limits>


using namespace std;
using namespace ku;
using boost::lexical_cast;
using namespace boost::posix_time;
using namespace boost::gregorian;


////////////////////////////////////////////////////////////////////////////////
// Type
////////////////////////////////////////////////////////////////////////////////

Type::Type(const string& pg_str)
{
    if (pg_str == "float8" || pg_str == "int4") {
        tag_ = NUMBER;
    } else if (pg_str == "text") {
        tag_ = STRING;
    } else if (pg_str == "bool") {
        tag_ = BOOL;
    } else if (pg_str == "timestamp") {
        tag_ = DATE;
    } else {
        KU_ASSERT_EQUAL(pg_str, "json");
        tag_ = JSON;
    }
}

bool Type::TraitsAreCompatible(Trait lhs, Trait rhs)
{
    return ((lhs == COMMON && rhs == COMMON) ||
            (lhs != COMMON && rhs != COMMON));
}


bool Type::IsApplicable(Trait trait) const
{
    return trait == COMMON || tag_ == NUMBER;
}


string Type::GetPgStr(Trait trait) const
{
    static const char* pg_strs[] =
        {"float8", "text", "bool", "timestamp(3)", "ku.json"};

    KU_ASSERT(tag_ < DUMMY && IsApplicable(trait));
    if (trait == COMMON)
        return pg_strs[tag_];
    return "int4";
}


string Type::GetKuStr(Trait trait) const
{
    static const char* ku_strs[] = {"number", "string", "bool", "date", "json"};

    KU_ASSERT(tag_ < DUMMY && IsApplicable(trait));
    if (trait == COMMON)
        return ku_strs[tag_];
    return trait == INTEGER ? "integer" : "serial";
}


string Type::GetCastFunc() const
{
    if (tag_ == DATE || tag_ == JSON)
        throw Error(Error::TYPE, "Cannot coerce any type to " + GetKuStr());
    return "ku.to_" + GetKuStr();
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

string BinaryOp::GetKuStr() const
{
    static const char* ku_strings[] = {
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

    return ku_strings[tag_];
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

    if (tag_ == SUM && (left_type == Type::STRING ||
                        right_type == Type::STRING))
        return Type::STRING;

    return Type::NUMBER;
}


Type BinaryOp::GetResultType(Type common_type) const
{
    if (IsComparison(tag_))
        return Type::BOOL;
    return common_type;
}


string BinaryOp::GetPgStr(Type common_type) const
{
    static const char* pg_strings[] = {
        "+", // SUM,
        "-", // SUB
        "*", // MUL
        "/", // DIV
        "%", // MOD
        "<", // LT
        ">", // GT
        "<=", // LE
        ">=", // GE
        "=", // EQ
        "<>", // NE
        "AND", // LOG_AND
        "OR"  // LOG_OR
    };

    if (tag_ == SUM && common_type == Type::STRING)
        return "||";
    return pg_strings[tag_];
}

////////////////////////////////////////////////////////////////////////////////
// UnaryOp
////////////////////////////////////////////////////////////////////////////////

string UnaryOp::GetKuStr() const
{
    static const char* ku_strings[] = {
        "+", // PLUS
        "-", // MINUS
        "!"  // NEG
    };

    return ku_strings[tag_];
}


Type UnaryOp::GetOpType() const
{
    switch (tag_) {
    case PLUS:
    case MINUS:
        return Type::NUMBER;
    default:
        KU_ASSERT_EQUAL(tag_, NEG);
        return Type::BOOL;
    }
}


string UnaryOp::GetPgStr() const
{
    switch (tag_) {
    case PLUS:
        return "+";
    case MINUS:
        return "-";
    default:
        KU_ASSERT_EQUAL(tag_, NEG);
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
    virtual PgLiter GetPgLiter() const = 0;
    virtual bool Get(double& d, string& s) const = 0;
};


Value::Impl::~Impl()
{
}


namespace
{
    class NumberValue : public Value::Impl {
    public:
        NumberValue(double repr)
            : repr_(repr) {}

        virtual Type GetType() const {
            return Type::NUMBER;
        }

        virtual PgLiter GetPgLiter() const {
            return PgLiter((repr_ != repr_
                            ? "'NaN'::float8"
                            : repr_ == numeric_limits<double>::infinity()
                            ? "'Infinity'::float8"
                            : repr_ == -numeric_limits<double>::infinity()
                            ? "'-Infinity'::float8"
                            : lexical_cast<string>(repr_)),
                           false);
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

        virtual PgLiter GetPgLiter() const {
            return PgLiter(repr_, true);
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

        virtual PgLiter GetPgLiter() const {
            return PgLiter(repr_ ? "true" : "false", false);
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

        virtual PgLiter GetPgLiter() const {
            ostringstream oss;
            oss << '\'' << repr_ << "'::" + GetType().GetPgStr();
            return PgLiter(oss.str(), false);
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
        if (type == Type::NUMBER) {
            return new NumberValue(d);
        } else {
            KU_ASSERT(type == Type::DATE);
            if (d != d)
                throw Error(Error::TYPE, "Invalid date");
            return new DateValue(DateValue::GetEpoch() + milliseconds(d));
        }
    }


    Value::Impl* CreateValueImplByString(Type type, const string& s)
    {
        if (type == Type::STRING) {
            return new StringValue(s);
        } else if (type == Type::NUMBER) {
            return new NumberValue(
                s.substr(0, 5) == "'NaN'"
                ? numeric_limits<double>::quiet_NaN()
                : s[0] == '('
                ? lexical_cast<double>(s.substr(1, s.size() - 2))
                : lexical_cast<double>(s));
        } else if (type == Type::BOOL) {
            KU_ASSERT(s == "true" || s == "false");
            return new BooleanValue(s == "true");
        } else if (type == Type::DATE) {
            return new DateValue(time_from_string(s));
        } else {
            KU_ASSERT(type == Type::JSON);
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
    KU_ASSERT(type == Type::BOOL);
    pimpl_.reset(new BooleanValue(b));
}


Value::~Value()
{
}


Type Value::GetType() const
{
    return pimpl_->GetType();
}


PgLiter Value::GetPgLiter() const
{
    return pimpl_->GetPgLiter();
}


bool Value::Get(double& d, string& s) const
{
    return pimpl_->Get(d, s);
}

////////////////////////////////////////////////////////////////////////////////
// Header
///////////////////////////////////////////////////////////////////////////////

ostream& ku::operator<<(ostream& os, const Header& header)
{
    os << '{';
    OmitInvoker print_sep((SepPrinter(os)));
    BOOST_FOREACH(const Attr& attr, header) {
        print_sep();
        os << '"' << attr.GetName() << "\": "
           << Quoted(attr.GetType().GetKuStr());
    }
    os << '}';
    return os;
}
