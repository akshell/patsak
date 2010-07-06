
// (c) 2008-2010 by Anton Korenyushkin

#ifndef COMMON_H
#define COMMON_H

#include "orset.h"

#include <boost/shared_ptr.hpp>
#include <boost/operators.hpp>

#include <iostream>
#include <map>


namespace ak
{
    ////////////////////////////////////////////////////////////////////////////
    // Error
    ////////////////////////////////////////////////////////////////////////////

    class Error : public std::runtime_error {
    public:
        enum Tag {
            TYPE,
            RANGE,

            VALUE,
            USAGE,
            NOT_IMPLEMENTED,

            NO_SUCH_APP,
            NO_SUCH_USER,
            CONVERSION,
            SOCKET,
            QUOTA,

            DB,
            REL_VAR_EXISTS,
            NO_SUCH_REL_VAR,
            CONSTRAINT,
            QUERY,
            ATTR_EXISTS,
            NO_SUCH_ATTR,
            ATTR_VALUE_REQUIRED,
            REL_VAR_DEPENDENCY,

            FS,
            PATH,
            ENTRY_EXISTS,
            NO_SUCH_ENTRY,
            ENTRY_IS_DIR,
            ENTRY_IS_NOT_DIR,
            FILE_IS_READ_ONLY
        };

        Error(Tag tag, const std::string& msg)
            : std::runtime_error(msg), tag_(tag) {}

        Tag GetTag() const { return tag_; }

    private:
        Tag tag_;
    };

    ////////////////////////////////////////////////////////////////////////////
    // Type
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
            INT,
            SERIAL,
            STRING,
            BOOL,
            DATE,
            JSON,
            DUMMY
        };

        Type(Tag tag) : tag_(tag) {} // implicit
        Type()        : tag_(DUMMY) {}
        Type(const std::string& pg_name);
        std::string GetPgName() const;
        std::string GetName() const;
        std::string GetCastFunc(Type from) const;

        bool IsNumeric() const {
            return tag_ == NUMBER || tag_ == INT || tag_ == SERIAL;
        }

        bool operator<(Type other) const {
            return tag_ < other.tag_;
        }

    private:
        Tag tag_;
    };

    ////////////////////////////////////////////////////////////////////////////
    // Value
    ////////////////////////////////////////////////////////////////////////////

    class Value: private boost::equality_comparable<Value> {
    public:
        class Impl;

        Value(Type type, double d);
        Value(Type type, int i);
        Value(Type type, const std::string& s);
        Value(Type type, const char* c);
        Value(Type type, bool b);
        ~Value();

        Type GetType() const;
        std::string GetPgLiter() const;
        bool Get(double& d, std::string& s) const;

    private:
        boost::shared_ptr<Impl> pimpl_;
    };

    ////////////////////////////////////////////////////////////////////////////
    // BinaryOp and UnaryOp
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
        std::string GetName() const;
        Type GetCommonType(Type left_type, Type right_type) const;
        Type GetResultType(Type common_type) const;
        std::string GetPgName(Type common_type) const;

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
        std::string GetName() const;
        Type GetOpType() const;
        std::string GetPgName() const;

    private:
        Tag tag_;
    };

    ////////////////////////////////////////////////////////////////////////////
    // Header
    ////////////////////////////////////////////////////////////////////////////

    class Attr : private boost::equality_comparable<Attr> {
    public:
        Attr(const std::string& name, Type type)
            : name_(name), type_(type) {}

        const std::string& GetName() const { return name_; }

        Type GetType() const { return type_; }

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
            throw Error(Error::NO_SUCH_ATTR,
                        "Attribute \"" + name + "\" does not exist");
        }
    };


    typedef orset<Attr, ByNameComparator<Attr>, ByNameFinder<Attr> > Header;

    ////////////////////////////////////////////////////////////////////////////
    // Draft
    ////////////////////////////////////////////////////////////////////////////

    class Draft {
    public:
        class Impl;

        Draft(Impl* pimpl);
        ~Draft();
        Value Get(Type type = Type::DUMMY) const;

    private:
        boost::shared_ptr<Impl> pimpl_;
    };

    ////////////////////////////////////////////////////////////////////////////
    // Typedefs
    ////////////////////////////////////////////////////////////////////////////

    typedef orset<std::string> StringSet;
    typedef std::vector<std::string> Strings;
    typedef std::vector<Value> Values;
    typedef std::vector<Type> Types;
    typedef std::vector<Draft> Drafts;
    typedef std::map<std::string, std::string> StringMap;
    typedef std::map<std::string, Draft> DraftMap;
    typedef std::vector<char> Chars;

    const size_t MINUS_ONE = static_cast<size_t>(-1);

    ////////////////////////////////////////////////////////////////////////////
    // InitCommon
    ////////////////////////////////////////////////////////////////////////////

    typedef std::string (*QuoteCallback)(const std::string& str);

    void InitCommon(QuoteCallback quote_cb);
}

#endif // COMMON_H
