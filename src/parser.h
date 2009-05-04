
// (c) 2008 by Anton Korenyushkin

/// \file parser.h
/// Query language parser interface

#ifndef PARSER_H
#define PARSER_H

#include "common.h"

#include <boost/variant.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/operators.hpp>

#include <string>
#include <vector>
#include <iosfwd>

namespace ku
{
    ////////////////////////////////////////////////////////////////////////////
    
    /// Base relation from DB table
    struct Base {
        std::string name;

        Base(const std::string& name) : name(name) {}
    };


    struct Union;
    struct Select;


    /// Relation
    typedef boost::variant<
        Base,
        boost::recursive_wrapper<Select>,
        boost::recursive_wrapper<Union> >
    Rel;

    ////////////////////////////////////////////////////////////////////////////
    
    class RangeVar : private boost::equality_comparable<RangeVar> {
    public:
        struct Impl {
            std::string name;
            Rel rel;

            Impl(const std::string& name, const Rel& rel)
                : name(name), rel(rel) {}
        };
        
        RangeVar(boost::shared_ptr<Impl> pimpl)
            : pimpl_(pimpl) {}

        RangeVar(const std::string& name, const Rel& rel)
            : pimpl_(new Impl(name, rel)) {}
        
        bool operator==(const RangeVar& other) const {
            return pimpl_ == other.pimpl_;
        }

        std::string GetName() const {
            return pimpl_->name;
        }
        
        const Rel& GetRel() const {
            return pimpl_->rel;
        }
        
    private:
        boost::shared_ptr<Impl> pimpl_;
    };
    
    ////////////////////////////////////////////////////////////////////////////

    /// Literal expr
    struct Liter {
        Value value;
        
        explicit Liter(const Value& value) : value(value) {}
    };


    /// Rangevar field expr or [multi]field proto
    struct MultiField {
        typedef std::vector<StringSet> Path;

        RangeVar rv;
        Path path;
        
        MultiField(const RangeVar& rv, const Path& path)
            : rv(rv), path(path)
            {
                KU_ASSERT(!path.empty());
                for (Path::const_iterator itr = path.begin();
                     itr != path.end();
                     ++itr)
                    KU_ASSERT(!itr->empty());
            }

        bool IsMulti()   const { return path.back().size() > 1; }
        bool IsForeign() const { return path.size() > 1;        }
    };

    
    /// Positional argument
    struct PosArg {
        unsigned pos;

        PosArg(unsigned pos) : pos(pos) {}
    };

    
    // Recursive Expr classes forward declarations
    struct Quant;
    struct Binary;
    struct Unary;
    struct Cond;
    

    /// Expression
    typedef boost::variant<
        Liter,
        MultiField,
        PosArg,
        boost::recursive_wrapper<Quant>,
        boost::recursive_wrapper<Binary>,
        boost::recursive_wrapper<Unary>,
        boost::recursive_wrapper<Cond> >
    Expr;

    ////////////////////////////////////////////////////////////////////////////

    /// Quantifier expr
    struct Quant {    
        typedef orset<RangeVar> RangeVars;
        
        bool flag;
        RangeVars rvs;
        Expr pred;

        Quant(bool flag, const RangeVars& rvs, const Expr& pred)
            : flag(flag), rvs(rvs), pred(pred) {}
    };


    /// Binary expr
    struct Binary {
        BinaryOp op;
        Expr left;
        Expr right;

        
        Binary(BinaryOp op, const Expr& left, const Expr& right)
            : op(op), left(left), right(right) {}
    };


    /// Unary expr
    struct Unary {
        UnaryOp op;
        Expr operand;
        
        Unary(UnaryOp op, const Expr& operand)
            : op(op), operand(operand) {}
    };


    /// Conditional (?:) expr
    struct Cond {
        Expr term;
        Expr yes;
        Expr no;

        Cond(const Expr& term, const Expr& yes, const Expr& no)
            : term(term), yes(yes), no(no) {}
    };    

    ////////////////////////////////////////////////////////////////////////////

    /// Representation of syntax {name: expr}
    struct NamedExpr {
        std::string name;
        Expr expr;

        NamedExpr(const std::string& name, const Expr& expr)
            : name(name), expr(expr) {}
    };

    
    typedef boost::variant<RangeVar, MultiField, NamedExpr> Proto;


    /// The most common relation
    struct Select {
        typedef std::vector<Proto> Protos;

        Protos protos;
        Expr expr;

        Select(const Protos& protos, const Expr& expr)
            : protos(protos), expr(expr) {}
    };
    

    /// Representaion of syntax [rel, ...]
    struct Union {
        Rel left;
        Rel right;

        Union(const Rel& left, const Rel& right)
            : left(left), right(right) {}
    };

    ////////////////////////////////////////////////////////////////////////////

    /// Parse rel from a string
    Rel ParseRel(const std::string& str);
    
    /// Parse expr from a string
    Expr ParseExpr(const std::string& str);

    std::ostream& operator<<(std::ostream& os, const RangeVar& rv);
    std::ostream& operator<<(std::ostream& os, const NamedExpr& ne);

    std::ostream& operator<<(std::ostream& os, const Base& base);
    std::ostream& operator<<(std::ostream& os, const Union& u);
    std::ostream& operator<<(std::ostream& os, const Select& select);


    std::ostream& operator<<(std::ostream& os, const Liter& liter);
    std::ostream& operator<<(std::ostream& os, const MultiField& multi_field);
    std::ostream& operator<<(std::ostream& os, const PosArg& pos_arg);
    std::ostream& operator<<(std::ostream& os, const Quant& quant);
    std::ostream& operator<<(std::ostream& os, const Binary& binary);
    std::ostream& operator<<(std::ostream& os, const Unary& unary);
    std::ostream& operator<<(std::ostream& os, const Cond& cond);
}

#endif // PARSER_H