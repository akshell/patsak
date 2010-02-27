
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

bool Type::TraitsAreCompatible(Trait lhs, Trait rhs)
{
    return ((lhs == COMMON && rhs == COMMON) ||
            (lhs != COMMON && rhs != COMMON));
}


bool Type::IsApplicable(Trait trait) const
{
    return trait == COMMON || tag_ == Type::NUMBER;
}


string Type::GetPgStr(Trait trait) const
{
    static const char* pg_strs[] = {"float8", "text", "bool", "timestamp(3)"};

    KU_ASSERT(tag_ < DUMMY && IsApplicable(trait));
    if (trait == COMMON)
        return pg_strs[tag_];
    return "int4";
}


string Type::GetKuStr(Trait trait) const
{
    static const char* ku_strs[] = {"number", "string", "bool", "date"};

    KU_ASSERT(tag_ < DUMMY && IsApplicable(trait));
    if (trait == COMMON)
        return ku_strs[tag_];
    return trait == INTEGER ? "integer" : "serial";
}


string Type::GetCastFunc() const
{
    return "ku.to_" + GetKuStr();
}


Type ku::PgType(const string& pg_type)
{
    if (pg_type == "float8" || pg_type == "int4") {
        return Type::NUMBER;
    } else if (pg_type == "text") {
        return Type::STRING;
    } else if (pg_type == "bool") {
        return Type::BOOL;
    } else {
        KU_ASSERT(pg_type == "timestamp");
        return Type::DATE;
    }
}


Type ku::KuType(const string& ku_type)
{
    if (ku_type == "number") {
        return Type::NUMBER;
    } else if (ku_type == "string") {
        return Type::STRING;
    } else if (ku_type == "bool") {
        return Type::BOOL;
    } else {
        KU_ASSERT(ku_type == "date");
        return Type::DATE;
    }
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
        KU_ASSERT(tag_ == NEG);
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
        KU_ASSERT(tag_ == NEG);
        return "NOT";
    }    
}

////////////////////////////////////////////////////////////////////////////////
// Value
////////////////////////////////////////////////////////////////////////////////

class Value::Impl {
public:
    virtual ~Impl();
    virtual Impl* Clone() const = 0;
    virtual Type GetType() const = 0;
    virtual PgLiter GetPgLiter() const = 0;
    virtual double GetDouble() const = 0;
    virtual string GetString() const = 0;
    virtual bool GetBool() const = 0;
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

        virtual Impl* Clone() const {
            return new NumberValue(*this);
        }
        
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
        
        virtual double GetDouble() const {
            return repr_;
        }
        
        virtual string GetString() const {
            return repr_ == repr_ ? lexical_cast<string>(repr_) : "NaN";
        }
        
        virtual bool GetBool() const {
            // false on 0 and NaN, otherwise true
            return repr_ == repr_ ? repr_ : false;
        }

    private:
        double repr_;
    };

    
    class StringValue : public Value::Impl {
    public:
        StringValue(const string& repr)
            : repr_(repr) {}
        
        virtual Impl* Clone() const {
            return new StringValue(*this);
        }
        
        virtual Type GetType() const {
            return Type::STRING;
        }
        
        virtual PgLiter GetPgLiter() const {
            return PgLiter(repr_, true);
        }
        
        virtual double GetDouble() const {
            istringstream iss(repr_);
            double result;
            iss >> result;
            if (iss.fail() || !iss.eof())
                return numeric_limits<double>::quiet_NaN();
            return result;
        }
        
        virtual string GetString() const {
            return repr_;
        }
        
        virtual bool GetBool() const {
            return !repr_.empty();
        }

    private:
        string repr_;
    };


    class BooleanValue : public Value::Impl {
    public:
        BooleanValue(bool repr)
            : repr_(repr) {}
        
        virtual Impl* Clone() const {
            return new BooleanValue(*this);
        }
        
        virtual Type GetType() const {
            return Type::BOOL;
        }
        
        virtual PgLiter GetPgLiter() const {
            return PgLiter(GetString(), false);
        }
        
        virtual double GetDouble() const {
            return repr_;
        }
        
        virtual string GetString() const {
            return repr_ ? "true" : "false";
        }
        
        virtual bool GetBool() const {
            return repr_;
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
        
        virtual Impl* Clone() const {
            return new DateValue(*this);
        }
        
        virtual Type GetType() const {
            return Type::DATE;
        }
        
        virtual PgLiter GetPgLiter() const {
            ostringstream oss;
            oss << '\'' << repr_ << "'::" + GetType().GetPgStr();
            return PgLiter(oss.str(), false);
        }
        
        virtual double GetDouble() const {
            time_duration diff(repr_ - GetEpoch());
            return diff.total_milliseconds();
        }
        
        virtual string GetString() const {
            ostringstream oss;
            oss.imbue(GetLocale());
            oss << repr_;
            return oss.str();
        }
        
        virtual bool GetBool() const {
            return true;
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
}


namespace
{
    Value::Impl* CreateValueImplByDouble(const Type& type, double d)
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

    
    Value::Impl* CreateValueImplByString(const Type& type, const string& s)
    {
        if (type == Type::STRING) {
            return new StringValue(s);
        } else if (type == Type::NUMBER) {
            return new NumberValue(s.substr(0, 5) == "'NaN'"
                                   ? numeric_limits<double>::quiet_NaN()
                                   : lexical_cast<double>(s));
        } else if (type == Type::BOOL) {
            KU_ASSERT(s == "true" || s == "false");
            return new BooleanValue(s == "true");
        } else {
            KU_ASSERT(type == Type::DATE);
            return new DateValue(time_from_string(s));
        }
    }
}


Value::Value(const Type& type, double d)
    : pimpl_(CreateValueImplByDouble(type, d))
{
}


Value::Value(const Type& type, int i)
    : pimpl_(CreateValueImplByDouble(type, i))
{
}


Value::Value(const Type& type, const std::string& s)
    : pimpl_(CreateValueImplByString(type, s))
{
}


Value::Value(const Type& type, const char* c)
    : pimpl_(CreateValueImplByString(type, c))
{
}


Value::Value(const Type& type, bool b)
{
    KU_ASSERT(type == Type::BOOL);
    pimpl_.reset(new BooleanValue(b));
}


Value::Value(const Value& other)
    : pimpl_(other.pimpl_->Clone())
{
}


Value::~Value()
{
}


Value& Value::operator=(const Value& other)
{
    pimpl_.reset(other.pimpl_->Clone());
    return *this;
}


Type Value::GetType() const
{
    return pimpl_->GetType();
}


PgLiter Value::GetPgLiter() const
{
    return pimpl_->GetPgLiter();
}


double Value::GetDouble() const
{
    return pimpl_->GetDouble();
}


string Value::GetString() const
{
    return pimpl_->GetString();
}


bool Value::GetBool() const
{
    return pimpl_->GetBool();
}


Value Value::Cast(Type cast_type) const
{
    if (cast_type == GetType())
        return *this;
    if (cast_type == Type::NUMBER)
        return Value(cast_type, GetDouble());
    if (cast_type == Type::STRING)
        return Value(cast_type, GetString());
    if (cast_type == Type::BOOL)
        return Value(cast_type, GetBool());
    KU_ASSERT(cast_type == Type::DATE);
    return Value(cast_type,
                 (GetType() == Type::STRING
                  ? ParseDate(GetString())
                  : GetDouble()));
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
