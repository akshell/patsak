// (c) 2008-2010 by Anton Korenyushkin

#ifndef COMMON_H
#define COMMON_H

#include "orset.h"

#include <boost/shared_ptr.hpp>

#include <iostream>
#include <stdexcept>


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
            NOT_IMPLEMENTED,
            QUOTA,

            DB,
            REL_VAR_EXISTS,
            NO_SUCH_REL_VAR,
            ATTR_EXISTS,
            NO_SUCH_ATTR,
            CONSTRAINT,
            QUERY,
            DEPENDENCY,

            FS,
            ENTRY_EXISTS,
            NO_SUCH_ENTRY,
            ENTRY_IS_FOLDER,
            ENTRY_IS_FILE,

            CONVERSION,
            SOCKET
        };

        Error(Tag tag, const std::string& msg)
            : std::runtime_error(msg), tag_(tag) {}

        Tag GetTag() const { return tag_; }

    private:
        Tag tag_;
    };

    ////////////////////////////////////////////////////////////////////////////
    // Type, Types, ReadType, and ReadPgType
    ////////////////////////////////////////////////////////////////////////////

    class Type {
    public:
        enum Tag {
            NUMBER,
            INTEGER,
            SERIAL,
            STRING,
            BOOLEAN,
            DATE,
            JSON,
            BINARY,
            DUMMY
        };

        Type(Tag tag = DUMMY) : tag_(tag) {} // implicit

        std::string GetPgName() const;
        std::string GetName() const;
        std::string GetCastFunc(Type from) const;

        bool IsNumeric() const {
            return tag_ == NUMBER || tag_ == INTEGER || tag_ == SERIAL;
        }

        bool operator==(Type other) const {
            return tag_ == other.tag_;
        }

        bool operator!=(Type other) const {
            return !(*this == other);
        }

    private:
        Tag tag_;
    };


    typedef std::vector<Type> Types;


    Type ReadType(const std::string& name);
    Type ReadPgType(const std::string& pg_name);

    ////////////////////////////////////////////////////////////////////////////
    // Value, Values, and ValuePtr
    ////////////////////////////////////////////////////////////////////////////

    class Value {
    public:
        class Impl;

        Value(Type type, double d);
        Value(Type type, int i);
        Value(Type type, const std::string& s);
        Value(Type type, const char* c);
        Value(Type type, bool b);
        ~Value();

        Type GetType() const;
        bool Get(double& d, std::string& s) const;
        void Print(std::ostream& os) const;

    protected:
        boost::shared_ptr<Impl> pimpl_;

        Value() {}
    };


    typedef std::vector<Value> Values;


    inline std::ostream& operator<<(std::ostream& os, const Value& value)
    {
        value.Print(os);
        return os;
    }


    class ValuePtr : public Value {
    public:
        ValuePtr() {}
        ValuePtr(const Value& value) : Value(value) {}

        const Value* operator->() const {
            return pimpl_ ? this : 0;
        }

        Value operator*() const {
            AK_ASSERT(pimpl_);
            return *this;
        }

        operator boost::shared_ptr<Impl>::unspecified_bool_type() const {
            return pimpl_;
        }
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
    // Named and NameGetter
    ////////////////////////////////////////////////////////////////////////////

    struct Named {
        std::string name;

        Named(const std::string& name) : name(name) {}
    };


    struct NameGetter : public std::unary_function<Named, std::string> {
        const std::string& operator()(const Named& named) const {
            return named.name;
        }
    };

    ////////////////////////////////////////////////////////////////////////////
    // Attr, Header, and GetAttr
    ////////////////////////////////////////////////////////////////////////////

    struct Attr : public Named {
        Type type;

        Attr(const std::string& name, Type type) : Named(name), type(type) {}
    };


    typedef orset<Attr, NameGetter> Header;


    template <typename T>
    const T& GetAttr(const orset<T, NameGetter>& set, const std::string& name)
    {
        const T* ptr = set.find(name);
        if (ptr)
            return *ptr;
        else
            throw Error(Error::NO_SUCH_ATTR,
                        "Attribute " + name + " doesn't exist");
    }

    ////////////////////////////////////////////////////////////////////////////
    // Draft, Drafts, NamedDraft, and DraftMap
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


    typedef std::vector<Draft> Drafts;


    struct NamedDraft : public Named {
        Draft draft;

        NamedDraft(const std::string& name, const Draft& draft)
            : Named(name), draft(draft) {}
    };


    typedef orset<NamedDraft, NameGetter> DraftMap;

    ////////////////////////////////////////////////////////////////////////////
    // NamedString and StringMap
    ////////////////////////////////////////////////////////////////////////////

    struct NamedString : public Named {
        std::string str;

        NamedString(const std::string& name, const std::string& str)
            : Named(name), str(str) {}
    };


    typedef orset<NamedString, NameGetter> StringMap;

    ////////////////////////////////////////////////////////////////////////////
    // GitPathPattern and GitPathPatterns
    ////////////////////////////////////////////////////////////////////////////

    struct GitPathPattern {
        std::string prefix;
        std::string suffix;
        std::string ending;

        GitPathPattern(const std::string& prefix,
                       const std::string& suffix,
                       const std::string& ending)
            : prefix(prefix)
            , suffix(suffix)
            , ending(ending) {}
    };


    typedef std::vector<GitPathPattern> GitPathPatterns;

    ////////////////////////////////////////////////////////////////////////////
    // Typedefs and constants
    ////////////////////////////////////////////////////////////////////////////

    typedef orset<std::string> StringSet;
    typedef std::vector<std::string> Strings;
    typedef std::vector<char> Chars;

    const size_t MINUS_ONE = static_cast<size_t>(-1);

    ////////////////////////////////////////////////////////////////////////////
    // InitCommon
    ////////////////////////////////////////////////////////////////////////////

    typedef std::string (*EscapeCallback)(const std::string& str, bool raw);

    void InitCommon(EscapeCallback escape_cb);
}

#endif // COMMON_H
