
// (c) 2008-2010 by Anton Korenyushkin

/// \file parser.cc
/// Query language parser implementation

#include "parser.h"
#include "utils.h"

#include <boost/spirit/include/classic.hpp>
#include <boost/spirit/include/phoenix1.hpp>
#include <boost/foreach.hpp>


using namespace std;
using namespace boost::spirit::classic;
using namespace phoenix;
using namespace ku;
using boost::shared_ptr;
using boost::variant;
using boost::static_visitor;
using boost::apply_visitor;
using boost::noncopyable;


////////////////////////////////////////////////////////////////////////////////
// Lookuper
////////////////////////////////////////////////////////////////////////////////

namespace {
    /// Range var definition data
    struct RVDef {
        StringSet id_list;
        Rel rel;

        RVDef(const StringSet& id_list, const Rel& rel)
            : id_list(id_list), rel(rel) {}
    };

    /// Stack based range var lookuper
    class Lookuper {
    public:
        typedef orset<RangeVar> RangeVars;
        
        Lookuper();

        RangeVars EnterScope(const RVDef& rv_def);
        void ExitScope();
        RangeVar Lookup(const string& name);
        void Reset();
    private:
        typedef vector<RangeVars> LookupStack;

        LookupStack stack_;
        RangeVar this_rv_;
    };
}

Lookuper::Lookuper()
    : stack_(1)
    , this_rv_("", Base(""))
{
}


/// Warning!!! Deep copy of rel happens here.
/// May be should be refactored.
Lookuper::RangeVars Lookuper::EnterScope(const RVDef& rv_def)
{
    RangeVars rvs;
    BOOST_FOREACH(const string& id, rv_def.id_list)
        rvs.add_sure(RangeVar(id, rv_def.rel));
    stack_.push_back(rvs);
    return rvs;
}


void Lookuper::ExitScope()
{
    KU_ASSERT(stack_.size() > 1);
    stack_.pop_back();
}


RangeVar Lookuper::Lookup(const string& name)
{
    if (name.empty())
        return this_rv_;
    // BOOST_REVERSE_FOREACH(const Scope& scope, scopes_) {
    for (LookupStack::const_reverse_iterator itr = stack_.rbegin();
         itr != stack_.rend();
         ++itr) {
        const RangeVars& rvs = *itr;
        BOOST_FOREACH(const RangeVar& rv, rvs) {
            if (rv.GetName() == name)
                return rv;
        }
    }
    RangeVar result(name, Base(name));
    stack_[0].add_sure(result);
    return result;
}

void Lookuper::Reset()
{
    KU_ASSERT(stack_.size() == 1);
    stack_.begin()->clear();
}

////////////////////////////////////////////////////////////////////////////////
// Parser
////////////////////////////////////////////////////////////////////////////////

// Compile-time printers for size optimization
template <int n>
struct P;

// P<sizeof(Expr)> p_expr;
// P<sizeof(Rel)> p_rel;
// P<sizeof(Select)> p_select;
// P<sizeof(Liter)> p_liter;
// P<sizeof(MultiField)> p_multi_field;
// P<sizeof(string)> p_string;
// P<sizeof(RangeVar)> p_rv;
// P<sizeof(shared_ptr<int>)> p_sp;
// P<sizeof(Proto)> p_proto;

namespace
{
    /// Lazy function struct for addition of unique values to orset
    struct CheckedAdd {
        template <typename ContT, typename ItemT>
        struct result {
            typedef void type;
        };

        template <typename ContT, typename ItemT>
        void operator()(ContT& c, const ItemT& item) const {
            if (!c.add_unsure(item))
                throw Error(Error::QUERY, "Duplicating items in a list");
        }
    };


    /// Lazy function struct for push_back-ing to vector
    struct PushBack {
        template <typename ContT, typename ItemT>
        struct result {
            typedef void type;
        };
        
        template <typename ContT, typename ItemT>
        void operator()(ContT& c, const ItemT& item) const {
            c.push_back(item);
        }
    };


    // Closure definitions
    
#define CLOSURE1(name, T1)                                             \
    struct name : public  boost::spirit::classic::closure<name, T1> {  \
        member1 val;                                                   \
    }

#define CLOSURE2(name, T1, T2, field2)                                  \
    struct name : public  boost::spirit::classic::closure<name, T1, T2> { \
        member1 val;                                                    \
        member2 field2;                                                 \
    }

#define CLOSURE3(name, T1, T2, field2, T3, field3)                      \
    struct name : public  boost::spirit::classic::closure<name, T1, T2, T3> { \
        member1 val;                                                    \
        member2 field2;                                                 \
        member3 field3;                                                 \
    }


    CLOSURE1(IdClosure, string);

    CLOSURE2(RVDefClosure, Wrapper<RVDef>, StringSet, id_list);

    CLOSURE1(PathClosure, MultiField::Path);
    CLOSURE1(PathEntryClosure, StringSet);
    
    CLOSURE1(RelClosure, Wrapper<Rel>);
    CLOSURE3(SelectClosure, Wrapper<Rel>, Select::Protos, protos,
             Wrapper<Expr>, expr);
    CLOSURE1(ProtosClosure, Select::Protos);
    CLOSURE1(ProtoClosure, Wrapper<Proto>);
    CLOSURE2(MultiFieldProtoClosure, Wrapper<Proto>, Wrapper<RangeVar>, rv);
    CLOSURE2(ExprProtoClosure, Wrapper<Proto>, string, name);    

    CLOSURE1(ExprClosure, Wrapper<Expr>);
    CLOSURE3(QuantClosure, Wrapper<Expr>, bool, flag, Quant::RangeVars, rvs);
    CLOSURE2(UnaryClosure, Wrapper<Expr>, Wrapper<UnaryOp>, op);
    CLOSURE2(BinaryClosure, Wrapper<Expr>, Wrapper<BinaryOp>, op);
    CLOSURE2(CondClosure, Wrapper<Expr>, Wrapper<Expr>, yes);
    CLOSURE2(FieldExprClosure, Wrapper<Expr>, Wrapper<RangeVar>, rv);
    CLOSURE2(StringLiterClosure, Wrapper<Expr>, string, str);
    
    
#undef CLOSURE1
#undef CLOSURE2
#undef CLOSURE3
    

    /// Parser class.
    class Parser : public noncopyable {
    public:
        Rel ParseRel(const string& str);
        Expr ParseExpr(const string& str);
        static Parser& GetInstance();
        
    private:
        rule<phrase_scanner_t, ExprClosure::context_t> expr;
        rule<phrase_scanner_t, RelClosure::context_t> rel_rule;
        rule<phrase_scanner_t, IdClosure::context_t> id;
        rule<phrase_scanner_t, RVDefClosure::context_t> rv_def;
        rule<phrase_scanner_t, PathClosure::context_t> path_rule;
        
        subrule< 1, PathClosure::context_t> path;
        subrule< 2, PathEntryClosure::context_t> path_entry;

        subrule< 3, RelClosure::context_t> rel;
        subrule< 4, SelectClosure::context_t> select_rel;
        subrule< 5, ProtosClosure::context_t> select_header;
        subrule< 6, ProtoClosure::context_t> rv_proto;
        subrule< 7, MultiFieldProtoClosure::context_t> multi_field_proto;
        subrule< 8, ExprProtoClosure::context_t> expr_proto;
        subrule< 9, RelClosure::context_t> union_rel;

        subrule<10, QuantClosure::context_t> quant_expr;
        subrule<11, CondClosure::context_t> cond_expr;
        subrule<12, BinaryClosure::context_t> log_or_expr;
        subrule<13, BinaryClosure::context_t> log_and_expr;
        subrule<14, BinaryClosure::context_t> eq_expr;
        subrule<15, BinaryClosure::context_t> rel_expr;
        subrule<16, BinaryClosure::context_t> add_expr;
        subrule<17, BinaryClosure::context_t> mul_expr;
        subrule<18, UnaryClosure::context_t> unary_expr;
        subrule<19, ExprClosure::context_t> prim_expr;
        subrule<20, ExprClosure::context_t> number_liter;
        subrule<21, StringLiterClosure::context_t> string_liter;
        subrule<22, ExprClosure::context_t> bool_liter;
        subrule<23, FieldExprClosure::context_t> field_expr;
        
        
        Lookuper lookuper_;
        
        Parser();
    };
}

////////////////////////////////////////////////////////////////////////////////

Parser& Parser::GetInstance()
{
    static Parser instance;
    return instance;
}   


Rel Parser::ParseRel(const string& str)
{
    lookuper_.Reset();
    Wrapper<Rel> rel_wrapper;
    if (!parse(str.c_str(),
               rel_rule[var(rel_wrapper) = arg1] >> end_p,
               space_p).full)
        throw Error(Error::QUERY, string("Wrong syntax: \"") + str + '"');
    return rel_wrapper.Get();
}


Expr Parser::ParseExpr(const string& str)
{
    lookuper_.Reset();
    Wrapper<Expr> expr_wrapper;
    if (!parse(str.c_str(),
               expr[var(expr_wrapper) = arg1] >> end_p,
               space_p).full)
        throw Error(Error::QUERY, string("Wrong syntax: \"") + str + '"');
    return expr_wrapper.Get();
}

////////////////////////////////////////////////////////////////////////////////
// Parser constructor with grammar definition
////////////////////////////////////////////////////////////////////////////////

Parser::Parser()
{
    distinct_parser<> keyword_p("a-zA-Z0-9_");
    function<CheckedAdd> checked_add;
    function<PushBack> push_back;

#define AUTO(var, value) typeof(value) var(value)

    AUTO(const enter_scope, bind(&Lookuper::EnterScope)(var(lookuper_), arg1));
    AUTO(const exit_scope, bind(&Lookuper::ExitScope)(var(lookuper_)));
    AUTO(const lookup, bind(&Lookuper::Lookup)(var(lookuper_), arg1));

#undef AUTO

    id = (lexeme_d[(alpha_p | '_') >> *(alnum_p | '_')])[
        id.val = construct_<string>(arg1, arg2)];


    rv_def = ('(' >> id[checked_add(rv_def.id_list, arg1)] % ',' >>
              keyword_p("in") >> rel_rule[
                  rv_def.val = construct_<RVDef>(rv_def.id_list, arg1)] >>
              ')');

    path_rule
        = (
            path = (
                path_entry[push_back(path.val, arg1)] % "->"),

            path_entry = (
                ('[' >> id[checked_add(path_entry.val, arg1)] % ',' >> ']') |
                id[checked_add(path_entry.val, arg1)])
        )[path_rule.val = arg1];
    
    // rel grammar definition
    rel_rule
        = (
            rel = (
                (keyword_p("for") >> rv_def[enter_scope] >>
                 rel[rel.val = arg1][exit_scope]) |
                union_rel[rel.val = arg1] |
                select_rel[rel.val = arg1]),

            union_rel = (
                keyword_p("union") >>
                '(' >> (rel[union_rel.val = arg1] >>
                        +(',' >>
                          rel[union_rel.val = (
                                  construct_<Union>(union_rel.val, arg1))])) >>
                ')'),
            
            select_rel = (
                select_header[
                    select_rel.protos = arg1,
                    select_rel.expr = (
                        construct_<Liter>(
                            construct_<Value>(Type::BOOL, true)))] >>
                !(keyword_p("where") >> expr[select_rel.expr = arg1]))[
                    select_rel.val = construct_<Select>(select_rel.protos,
                                                        select_rel.expr)],

            select_header = (
                ('{' >> !(
                    (expr_proto[push_back(select_header.val, arg1)] |
                     multi_field_proto[push_back(select_header.val, arg1)] |
                     rv_proto[push_back(select_header.val, arg1)]) % ',') >>
                 '}') |
                (multi_field_proto[push_back(select_header.val, arg1)] |
                 rv_proto[push_back(select_header.val, arg1)])),

            multi_field_proto = (
                id[multi_field_proto.rv = lookup] >>
                (('.' >> epsilon_p(~ch_p('['))) | epsilon_p('[')) >>
                path_rule[
                    multi_field_proto.val = (
                        construct_<MultiField>(multi_field_proto.rv, arg1))]),
            
            expr_proto = (
                id[expr_proto.name = arg1] >> ':' >>
                expr[expr_proto.val = construct_<NamedExpr>(expr_proto.name,
                                                            arg1)]),

            rv_proto = (
                id[rv_proto.val = lookup])
                
            
            )[rel_rule.val = arg1];


#define UNARY(subexpr)                                          \
    (subexpr[EXPR.val = construct_<Unary>(EXPR.op, (arg1))])
    
#define BINARY(subexpr)                                 \
    (subexpr[EXPR.val = construct_<Binary>(EXPR.op,     \
                                           (EXPR.val),  \
                                           (arg1))])

#define SUBST(subexpr) (subexpr[EXPR.val = arg1])

#define OP(parser, code) (parser[EXPR.op = code])

#define LITER(parser, type, value)                                      \
    parser[EXPR.val = construct_<Liter>(construct_<Value>(type, value))]

    // expr grammar definition
    expr
        = (

#define EXPR quant_expr

            EXPR = (
                ((keyword_p("forsome")[EXPR.flag = val(false)] |
                  keyword_p("forall")[EXPR.flag = val(true)]) >>
                 ((rv_def[EXPR.rvs = enter_scope] >>
                   quant_expr[
                       EXPR.val = construct_<Quant>(EXPR.flag,
                                                    EXPR.rvs,
                                                    arg1),
                       exit_scope]) |
                  ('(' >> (
                      id[checked_add(EXPR.rvs, lookup)] % ',') >>
                   ')' >>
                   quant_expr[
                       EXPR.val = construct_<Quant>(EXPR.flag,
                                                    EXPR.rvs,
                                                    arg1)]))) |
                SUBST(cond_expr)),
            
#undef EXPR
#define EXPR cond_expr
            
            EXPR = (
                SUBST(log_or_expr) >>
                !('?' >> expr[cond_expr.yes = arg1] >>
                  ':' >> cond_expr[
                      cond_expr.val = (
                          construct_<Cond>((cond_expr.val),
                                           (cond_expr.yes),
                                           (arg1)))])),
            
#undef EXPR
#define EXPR log_or_expr

            EXPR = (
                SUBST(log_and_expr) >>
                *(OP(str_p("||"), BinaryOp::LOG_OR) >>
                  BINARY(log_and_expr))),
            
#undef EXPR
#define EXPR log_and_expr

            EXPR = (
                SUBST(eq_expr) >>
                *(OP(str_p("&&"), BinaryOp::LOG_AND) >>
                  BINARY(eq_expr))),
            
#undef EXPR
#define EXPR eq_expr

            EXPR = (
                SUBST(rel_expr) >>
                *((OP(str_p("=="), BinaryOp::EQ) |
                   OP(str_p("!="), BinaryOp::NE)) >>
                  BINARY(rel_expr))),
            
#undef EXPR
#define EXPR rel_expr

            EXPR = (
                SUBST(add_expr) >>
                *((OP(str_p("<="), BinaryOp::LE) |
                   OP(str_p(">="), BinaryOp::GE) |
                   OP(ch_p('<'), BinaryOp::LT) |
                   OP(ch_p('>'), BinaryOp::GT)) >>
                  BINARY(add_expr))),

#undef EXPR
#define EXPR add_expr
            
            EXPR = (
                SUBST(mul_expr) >>
                *((OP(ch_p('+'), BinaryOp::SUM) |
                   OP(ch_p('-'), BinaryOp::SUB)) >>
                  BINARY(mul_expr))),

#undef EXPR
#define EXPR mul_expr

            EXPR = (
                SUBST(unary_expr) >>
                *((OP(ch_p('*'), BinaryOp::MUL) |
                   OP(ch_p('/'), BinaryOp::DIV) |
                   OP(ch_p('%'), BinaryOp::MOD)) >>
                  BINARY(unary_expr))),

#undef EXPR
#define EXPR unary_expr
            
            EXPR = (
                ((OP(ch_p('+'), UnaryOp::PLUS) |
                  OP(ch_p('-'), UnaryOp::MINUS) |
                  OP(ch_p('!'), UnaryOp::NEG)) >>
                 UNARY(prim_expr)) |
                SUBST(prim_expr)),
                                           
#undef EXPR
#define EXPR prim_expr
            
            EXPR = (
                SUBST(number_liter) |
                SUBST(bool_liter)   |
                SUBST(string_liter) |
                '(' >> SUBST(expr) >> ')' |
                lexeme_d['$' >> uint_p[EXPR.val = construct_<PosArg>(arg1)]] |
                ch_p('$')[EXPR.val = construct_<PosArg>(1)] |
                SUBST(field_expr)),

#undef EXPR
#define EXPR number_liter
            
            EXPR = (
                LITER(real_p, Type::NUMBER, arg1)),

#undef EXPR
#define EXPR string_liter
            
            EXPR = (
                lexeme_d[
                    LITER((confix_p('"',
                                    *('"' | c_escape_ch_p[EXPR.str += arg1]),
                                    '"') |
                           confix_p('\'',
                                    *('\'' | c_escape_ch_p[EXPR.str += arg1]),
                                    '\'')),
                          Type::STRING,
                          EXPR.str)]),

#undef EXPR
#define EXPR bool_liter
            
            EXPR = (
                LITER(keyword_p("true"), Type::BOOL, true) |
                LITER(keyword_p("false"), Type::BOOL, false)),

#undef EXPR
#define EXPR field_expr

            EXPR = (
                ((id[EXPR.rv = lookup] >>
                  (('.' >> epsilon_p(~ch_p('['))) | epsilon_p('['))) |
                 epsilon_p[EXPR.rv = lookup("")]) >>
                path_rule[EXPR.val = construct_<MultiField>(EXPR.rv, arg1)])
            
            )[expr.val = arg1];

#undef UNARY
#undef BINARY
#undef SUBST
#undef LITER
#undef OP
#undef EXPR
}

////////////////////////////////////////////////////////////////////////////////
// Print operators
////////////////////////////////////////////////////////////////////////////////

namespace {
    class Bracer {
    public:
        Bracer(ostream& os, const string& name)
            : os_(os) { os_ << '(' << name; }
        ~Bracer()     { os_ << ')'; }
    private:
        ostream& os_;
    };
}

////////////////////////////////////////////////////////////////////////////////

ostream& ku::operator<<(ostream& os, const RangeVar& rv)
{
    Bracer b(os, "rv");
    if (rv.GetName().empty())
        os << " *this*";
    else
        os << ' ' << rv.GetName()
           << ' ' << rv.GetRel();
    return os;
}


ostream& ku::operator<<(ostream& os, const NamedExpr& ne)
{
    Bracer b(os, "as");
    os << ' ' << ne.name
       << ' ' << ne.expr;
    return os;
}

////////////////////////////////////////////////////////////////////////////////

ostream& ku::operator<<(ostream& os, const Base& base)
{
    Bracer b(os, "base");
    os << ' ' << base.name;
    return os;
}


ostream& ku::operator<<(ostream& os, const Union& un)
{
    Bracer b(os, "union");
    os << ' ' << un.left
       << ' ' << un.right;
    return os;
}


ostream& ku::operator<<(ostream& os, const Select& select)
{
    Bracer b(os, "where");
    os << ' ' << select.expr;
    BOOST_FOREACH(const Proto& proto, select.protos)
        os << ' ' << proto;
    return os;
}

////////////////////////////////////////////////////////////////////////////////

ostream& ku::operator<<(ostream& os, const Liter& liter)
{
    string str(liter.value.GetString());
    if (liter.value.GetType() == Type::STRING)
        os << '"' << str << '"';
    else
        os << str;
    return os;
}


namespace
{
    void PrintStringSet(ostream& os, const StringSet& ss)
    {
        BOOST_FOREACH(const string& str, ss)
            os << ' ' << str;
    }


    void PrintKeys(ostream& os, const MultiField& multi_field, size_t n)
    {
        os << ' ';
        if (n) {
            Bracer b(os, "key");
            PrintStringSet(os, multi_field.path[n - 1]);
            PrintKeys(os, multi_field, n - 1);
        } else
            os << multi_field.rv;
    }
}


ostream& ku::operator<<(ostream& os, const MultiField& multi_field)
{
    Bracer b(os, multi_field.IsMulti() ? "fields" : "field");
    PrintStringSet(os, multi_field.path.back());
    PrintKeys(os, multi_field, multi_field.path.size() - 1);
    return os;
}


ostream& ku::operator<<(ostream& os, const PosArg& pos_arg)
{
    os << '$' << pos_arg.pos;
    return os;
}


ostream& ku::operator<<(ostream& os, const Quant& quant)
{
    Bracer b(os, quant.flag ? "always" : "once");
    os << ' ' << quant.pred;
    BOOST_FOREACH(const RangeVar& rv, quant.rvs)
        os << ' ' << rv;
    return os;
}


ostream& ku::operator<<(ostream& os, const Binary& binary)
{
    Bracer b(os, binary.op.GetKuStr());
    os << ' ' << binary.left
       << ' ' << binary.right;
    return os;
}


ostream& ku::operator<<(ostream& os, const Unary& unary)
{
    Bracer b(os, unary.op.GetKuStr());
    os << ' ' << unary.operand;
    return os;
}


ostream& ku::operator<<(ostream& os, const Cond& cond)
{
    Bracer b(os, "?");
    os << ' ' << cond.term
       << ' ' << cond.yes
       << ' ' << cond.no;
    return os;
}

////////////////////////////////////////////////////////////////////////////////
// Entry points
////////////////////////////////////////////////////////////////////////////////

Rel ku::ParseRel(const string& str)
{
    return Parser::GetInstance().ParseRel(str);
}


Expr ku::ParseExpr(const string& str)
{
    return Parser::GetInstance().ParseExpr(str);
}
