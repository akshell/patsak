
// (c) 2008-2010 by Anton Korenyushkin

#ifndef COMMON_H
#define COMMON_H

#include "orset.h"

#include <boost/scoped_ptr.hpp>
#include <boost/operators.hpp>

#include <iostream>
#include <map>


namespace ku
{
    ////////////////////////////////////////////////////////////////////////////
    
    class Error : public std::runtime_error {
    public:
        enum Tag {
            TYPE,
            RANGE,

            VALUE,
            USAGE,
            NOT_IMPLEMENTED,

            REQUEST_APP,
            REQUEST_HOST,
            NO_SUCH_APP,
            NO_SUCH_USER,
            CONVERSION,

            DB,
            DB_QUOTA,
            REL_VAR_EXISTS,
            NO_SUCH_REL_VAR,
            REL_VAR_DEPENDENCY,
            CONSTRAINT,
            FIELD,
            QUERY,

            FS,
            FS_QUOTA,
            PATH,
            ENTRY_EXISTS,
            NO_SUCH_ENTRY,
            ENTRY_IS_DIR,
            ENTRY_IS_NOT_DIR,
            DIR_IS_NOT_EMPTY,
            FILE_IS_READ_ONLY
        };
        
        Error(Tag tag, const std::string& msg)
            : std::runtime_error(msg), tag_(tag) {}
        
        Tag GetTag() const { return tag_; }

    private:
        Tag tag_;
    };

    ////////////////////////////////////////////////////////////////////////////

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
            BOOL,
            DATE,
            DUMMY
        };

        enum Trait {
            COMMON,
            INTEGER,
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


    Type PgType(const std::string& pg_type);
    Type KuType(const std::string& ku_type);

    ////////////////////////////////////////////////////////////////////////////

    class PgLiter {
    public:
        std::string str;
        bool quote_me;
        
        explicit PgLiter(const std::string& str, bool quote_it)
            : str(str), quote_me(quote_it) {}
    };

    ////////////////////////////////////////////////////////////////////////////

    // To be defined by an upper level
    double ParseDate(const std::string& str);


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

        void not_found(const std::string& name) const {
            throw Error(Error::FIELD, "Field \"" + name + "\" does not exist");
        }
    };


    typedef orset<Attr, ByNameComparator<Attr>, ByNameFinder<Attr> > Header;


    // Get type of an attribute in a header. Throw error if attribute
    // with such name does not exist
    inline Type GetAttrType(const Header& header, const std::string& attr_name)
    {
        return header.find(attr_name).GetType();
    }
    
    
    // Print header in ku style
    std::ostream& operator<<(std::ostream& os, const Header& header);

    ////////////////////////////////////////////////////////////////////////////

    // Useful typedefs
    typedef orset<std::string> StringSet;
    typedef std::vector<std::string> Strings;
    typedef std::vector<StringSet> StringSets;
    typedef std::vector<Value> Values;
    typedef std::vector<Type> Types;
    typedef std::map<std::string, std::string> StringMap;
    typedef std::map<std::string, Value> ValueMap;
    typedef std::vector<char> Chars;

    const size_t MINUS_ONE = static_cast<size_t>(-1);
}

#endif // COMMON_H
