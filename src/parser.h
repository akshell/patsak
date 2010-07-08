
// (c) 2008-2010 by Anton Korenyushkin

#ifndef PARSER_H
#define PARSER_H

#include "common.h"

#include <boost/variant.hpp>
#include <boost/shared_ptr.hpp>


namespace ak
{
    ////////////////////////////////////////////////////////////////////////////
    // Rel
    ////////////////////////////////////////////////////////////////////////////

    struct Base {
        std::string name;

        Base(const std::string& name) : name(name) {}
    };


    struct Union;
    struct Select;


    typedef boost::variant<
        Base,
        boost::recursive_wrapper<Select>,
        boost::recursive_wrapper<Union> >
    Rel;

    ////////////////////////////////////////////////////////////////////////////
    // RangeVar
    ////////////////////////////////////////////////////////////////////////////

    class RangeVar {
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

        bool operator==(RangeVar other) const {
            return pimpl_ == other.pimpl_;
        }

        bool operator!=(RangeVar other) const {
            return !(*this == other);
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


    typedef orset<RangeVar> RangeVarSet;

    ////////////////////////////////////////////////////////////////////////////
    // Expr
    ////////////////////////////////////////////////////////////////////////////

    struct Liter {
        Value value;

        explicit Liter(const Value& value) : value(value) {}
    };


    // Rangevar field expr or [multi]field proto
    struct MultiField {
        typedef std::vector<StringSet> Path;

        RangeVar rv;
        Path path;

        MultiField(const RangeVar& rv, const Path& path)
            : rv(rv), path(path)
            {
                AK_ASSERT(!path.empty());
                for (Path::const_iterator itr = path.begin();
                     itr != path.end();
                     ++itr)
                    AK_ASSERT(!itr->empty());
            }

        bool IsMulti()   const { return path.back().size() > 1; }
        bool IsForeign() const { return path.size() > 1;        }
    };


    struct PosArg {
        unsigned pos;

        PosArg(unsigned pos) : pos(pos) {}
    };


    struct Quant;
    struct Binary;
    struct Unary;
    struct Cond;


    typedef boost::variant<
        Liter,
        MultiField,
        PosArg,
        boost::recursive_wrapper<Quant>,
        boost::recursive_wrapper<Binary>,
        boost::recursive_wrapper<Unary>,
        boost::recursive_wrapper<Cond> >
    Expr;


    struct Quant {
        bool flag;
        RangeVarSet rv_set;
        Expr pred;

        Quant(bool flag, const RangeVarSet& rv_set, const Expr& pred)
            : flag(flag), rv_set(rv_set), pred(pred) {}
    };


    struct Binary {
        BinaryOp op;
        Expr left;
        Expr right;


        Binary(BinaryOp op, const Expr& left, const Expr& right)
            : op(op), left(left), right(right) {}
    };


    struct Unary {
        UnaryOp op;
        Expr operand;

        Unary(UnaryOp op, const Expr& operand)
            : op(op), operand(operand) {}
    };


    struct Cond {
        Expr term;
        Expr yes;
        Expr no;

        Cond(const Expr& term, const Expr& yes, const Expr& no)
            : term(term), yes(yes), no(no) {}
    };

    ////////////////////////////////////////////////////////////////////////////
    // Proto
    ////////////////////////////////////////////////////////////////////////////

    struct NamedExpr {
        std::string name;
        Expr expr;

        NamedExpr(const std::string& name, const Expr& expr)
            : name(name), expr(expr) {}
    };


    typedef boost::variant<RangeVar, MultiField, NamedExpr> Proto;
    typedef std::vector<Proto> Protos;


    struct Select {
        Protos protos;
        Expr expr;

        Select(const Protos& protos, const Expr& expr)
            : protos(protos), expr(expr) {}
    };


    struct Union {
        Rel left;
        Rel right;

        Union(const Rel& left, const Rel& right)
            : left(left), right(right) {}
    };

    ////////////////////////////////////////////////////////////////////////////
    // API
    ////////////////////////////////////////////////////////////////////////////

    Rel ParseRel(const std::string& str);
    Expr ParseExpr(const std::string& str);
}

#endif // PARSER_H
