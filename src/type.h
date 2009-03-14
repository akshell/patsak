
// (c) 2008 by Anton Korenyushkin

/// \file type.h
/// Ku type abstraction

#ifndef TYPE_H
#define TYPE_H

#include "orset.h"
#include "error.h"

#include <boost/scoped_ptr.hpp>
#include <boost/operators.hpp>

#include <string>
#include <iosfwd>


namespace ku
{
    ////////////////////////////////////////////////////////////////////////////

    /// Type abstraction
    class Type : private
    boost::equivalent<
        Type,
        boost::less_than_comparable<
            Type,
            boost::equality_comparable<Type> > > {
    public:
        enum Tag {
            NUMBER,
            STRING,
            BOOLEAN,
            DATE,
            DUMMY
        };

        /// Additional type information
        enum Trait {
            COMMON,
            INT,
            SERIAL
        };

        static bool TraitsAreCompatible(Trait lhs, Trait rhs);

        Type(Tag tag) : tag_(tag) {} // implicit
        Type()        : tag_(DUMMY) {}
        bool IsApplicable(Trait trait) const;
        std::string GetPgStr(Trait trait = COMMON) const;
        std::string GetKuStr(Trait trait = COMMON) const;
        std::string GetCastFunc() const;

        bool operator<(Type other) const {
            return tag_ < other.tag_;
        }
        
    private:
        Tag tag_;
    };


    /// Type from pg type string
    Type PgType(const std::string& pg_type);

    
    /// Type from ku type string
    Type KuType(const std::string& ku_type);

    ////////////////////////////////////////////////////////////////////////////

    /// Postgres literal. Quoting-needed flag is included.
    class PgLiter {
    public:
        std::string str;
        bool quote_me;
        
        explicit PgLiter(const std::string& str, bool quote_it)
            : str(str), quote_me(quote_it) {}
    };

    ////////////////////////////////////////////////////////////////////////////

    /// Value which could be placed into DB
    class Value: private boost::equality_comparable<Value> {
    public:
        class Impl;

        Value(const Type& type, double d);
        Value(const Type& type, int i);
        Value(const Type& type, const std::string& s);
        Value(const Type& type, const char* c);
        Value(const Type& type, bool b);

        Value(const Value& other);
        ~Value();
        Value& operator=(const Value& other);
        
        Type GetType() const;
        PgLiter GetPgLiter() const;
        double GetDouble() const;
        std::string GetString() const;
        bool GetBool() const;
        Value Cast(Type cast_type) const;

    private:
        boost::scoped_ptr<Impl> pimpl_;
    };

    ////////////////////////////////////////////////////////////////////////////

    /// Binary operation
    class BinaryOp {
    public:
        enum Tag {
            SUM,
            SUB,
            MUL,
            DIV,
            MOD,
            LT,
            GT,
            LE,
            GE,
            EQ,
            NE,
            LOG_AND,
            LOG_OR            
        };

        BinaryOp(Tag tag) : tag_(tag) {} // implicit
        std::string GetKuStr() const;
        Type GetCommonType(Type left_type, Type right_type) const;
        Type GetResultType(Type common_type) const;
        std::string GetPgStr(Type common_type) const;

    private:
        Tag tag_;
    };


    /// Unary operation
    class UnaryOp {
    public:
        enum Tag {
            PLUS,
            MINUS,
            NEG
        };

        UnaryOp(Tag tag) : tag_(tag) {} // implicit
        std::string GetKuStr() const;
        Type GetOpType() const;
        std::string GetPgStr() const;

    private:
        Tag tag_;
    };        

    ////////////////////////////////////////////////////////////////////////////
    
    /// Relational attribute (name and type)
    class Attr : private boost::equality_comparable<Attr> {
    public:
        Attr(const std::string& name, Type type)
            : name_(name), type_(type) {}

        const std::string& GetName() const { return name_; }
        Type GetType() const { return type_; }
            
        bool operator==(const Attr& other) const {
            return name_ == other.name_ && type_ == other.type_;
        }

    private:
        std::string name_;
        Type type_;
    };


    template <typename T>
    class ByNameComparator : public std::binary_function<T, T, bool> {
    public:
        bool operator()(const T& lhs, const T& rhs) const {
            return lhs.GetName() == rhs.GetName();
        }
    };

    
    template <typename T>
    class ByNameFinder : public std::binary_function<T, std::string, bool> {
    public:
        bool operator()(const T& item, const std::string& name) const {
            return item.GetName() == name;
        }

        std::exception not_found_exception(const std::string& name) const {
            return Error("Field " + name + " does not exist");
        }
    };


    /// Relation attributes
    typedef orset<Attr, ByNameComparator<Attr>, ByNameFinder<Attr> > Header;


    /// Get type of an attribute in a header. Throw error if attribute
    /// with such name does not exist
    inline Type GetAttrType(const Header& header, const std::string& attr_name)
    {
        return header.find(attr_name).GetType();
    }
    
    
    /// Print header in ku style
    std::ostream& operator<<(std::ostream& os, const Header& header);
}

#endif // TYPE_H
