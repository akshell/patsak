
// (c) 2008-2010 by Anton Korenyushkin

#include "translator.h"
#include "parser.h"
#include "utils.h"

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>


using namespace std;
using namespace ak;
using boost::static_visitor;
using boost::apply_visitor;
using boost::lexical_cast;


////////////////////////////////////////////////////////////////////////////////
// Globals
////////////////////////////////////////////////////////////////////////////////

namespace
{
    const char* THIS_NAME = "@";

    GetHeaderCallback get_header_cb = 0;
    FollowReferenceCallback follow_reference_cb = 0;
}

////////////////////////////////////////////////////////////////////////////////
// Cast
////////////////////////////////////////////////////////////////////////////////

namespace
{
    string Cast(const string& expr_str, Type from_type, Type to_type)
    {
        string cast_func(to_type.GetCastFunc(from_type));
        return cast_func.empty() ? expr_str : cast_func + '(' + expr_str + ')';
    }
}

////////////////////////////////////////////////////////////////////////////////
// Control, RelTranslator, and ExprTranslator declarations
////////////////////////////////////////////////////////////////////////////////

namespace
{
    // Translation control class. One instance per translation.
    // Manages the translation process,
    // provides means for output and db access.
    class Control {
    public:
        struct BindUnit {
            RangeVar rv;
            Header header;

            BindUnit(const RangeVar& rv, const Header& header)
                : rv(rv), header(header) {}
        };

        typedef vector<BindUnit> BindData;

        // Scope for binding rangevars.
        // Must only be stack allocated
        class BindScope {
        public:
            BindScope(Control& control, const BindData& bind_data);
            ~BindScope();

        private:
            Control& control_;
        };

        // Scope for catching output.
        // Must only be stack allocated
        class StringScope {
        public:
            StringScope(Control& control, string& str);
            ~StringScope();
            string GetStr() const;

        private:
            Control& control_;
            string& str_;
            ostream* old_os_ptr_;
            ostringstream os_;
        };

        Control(const Drafts& params);

        const Header& LookupBind(const RangeVar& rv) const;
        Header TranslateRel(const Rel& rel);

        Type TranslateExpr(const Expr& expr,
                           const RangeVar* this_rv_ptr,
                           Type needed_type = Type::DUMMY);

        Type PrintParam(size_t pos, Type needed_type = Type::DUMMY);

        operator ostream&() const;
        template <typename T> Control& operator<<(const T& t);

    private:
        typedef vector<BindData> BindStack;

        const Drafts& params_;
        BindStack bind_stack_;
        ostream* os_ptr_;
    };


    class RelTranslator : public static_visitor<Header> {
    public:
        RelTranslator(Control& control)
            : control_(control) {}

        Header operator()(const Base& base) const;
        Header operator()(const Select& select) const;
        Header operator()(const Union& un) const;

    private:
        Control& control_;

        Control::BindData BindSelect(const Select& select) const;
    };


    class ExprTranslator : public static_visitor<Type> {
    public:
        ExprTranslator(Control& control, const RangeVar* this_rv_ptr);

        Type operator()(const Liter& liter) const;
        Type operator()(const MultiField& multi_field) const;
        Type operator()(const PosArg& pos_arg) const;
        Type operator()(const Quant& quant) const;
        Type operator()(const Binary& binary) const;
        Type operator()(const Unary& unary) const;
        Type operator()(const Cond& cond) const;

    private:
        Control& control_;
        const RangeVar* this_rv_ptr_;
    };
}

////////////////////////////////////////////////////////////////////////////////
// Control definitons
////////////////////////////////////////////////////////////////////////////////

Control::Control(const Drafts& params)
    : params_(params)
    , os_ptr_(0)
{
}


const Header& Control::LookupBind(const RangeVar& rv) const
{
    for (BindStack::const_reverse_iterator itr = bind_stack_.rbegin();
         itr != bind_stack_.rend();
         ++itr) {
        BOOST_FOREACH(const BindUnit& bind_unit, *itr)
            if (bind_unit.rv == rv)
                return bind_unit.header;
    }
    throw Error(Error::QUERY, "Rangevar \"" + rv.GetName() + "\" is unbound");

}


Header Control::TranslateRel(const Rel& rel)
{
    return apply_visitor(RelTranslator(*this), rel);
}


Type Control::TranslateExpr(const Expr& expr,
                            const RangeVar* this_rv_ptr,
                            Type needed_type)
{
    if (const PosArg* pos_arg_ptr = boost::get<PosArg>(&expr))
        return PrintParam(pos_arg_ptr->pos, needed_type);
    if (needed_type == Type::DUMMY)
        return apply_visitor(ExprTranslator(*this, this_rv_ptr), expr);
    string expr_str;
    Type type;
    {
        StringScope string_scope(*this, expr_str);
        type = TranslateExpr(expr, this_rv_ptr);
    }
    *this << Cast(expr_str, type, needed_type);
    return needed_type;
}


Type Control::PrintParam(size_t pos, Type needed_type)
{
    if (pos == 0)
        throw Error(Error::QUERY, "Position 0 is invalid");
    if (pos > params_.size())
        throw Error(
            Error::QUERY,
            "Position " + lexical_cast<string>(pos) + " is out of range");
    Value value(params_[pos - 1].Get(needed_type));
    *this << value.GetPgLiter();
    return value.GetType();
}


Control::operator ostream&() const
{
    AK_ASSERT(os_ptr_);
    return *os_ptr_;
}


template <typename T>
Control& Control::operator<<(const T& t)
{
    static_cast<ostream&>(*this) << t;
    return *this;
}

////////////////////////////////////////////////////////////////////////////////
// Control::BindScope definitions
////////////////////////////////////////////////////////////////////////////////

Control::BindScope::BindScope(Control& control, const BindData& bind_data)
    : control_(control)
{
    control.bind_stack_.push_back(bind_data);
}


Control::BindScope::~BindScope()
{
    AK_ASSERT(!control_.bind_stack_.empty());
    control_.bind_stack_.pop_back();
}

////////////////////////////////////////////////////////////////////////////////
// Control::StringScope definitions
////////////////////////////////////////////////////////////////////////////////

Control::StringScope::StringScope(Control& control, string& str)
    : control_(control), str_(str), old_os_ptr_(control.os_ptr_)
{
    control.os_ptr_ = &os_;
}


Control::StringScope::~StringScope()
{
    str_ = os_.str();
    control_.os_ptr_ = old_os_ptr_;
}

////////////////////////////////////////////////////////////////////////////////
// ExprRangeVarCollector
////////////////////////////////////////////////////////////////////////////////

namespace
{
    typedef orset<RangeVar> RangeVarSet;


    // Collect unbound rangevars from expression
    class ExprRangeVarCollector : public static_visitor<void> {
    public:
        ExprRangeVarCollector(RangeVarSet& rvs)
            : rvs_(rvs) {}

        void operator()(const Liter& liter) const;
        void operator()(const MultiField& multi_field) const;
        void operator()(const PosArg& pos_arg) const;
        void operator()(const Quant& quant);
        void operator()(const Binary& binary);
        void operator()(const Unary& unary);
        void operator()(const Cond& cond);

    private:
        RangeVarSet& rvs_;
        vector<const Quant::RangeVars*> quant_rvs_stack_;
    };
}


void ExprRangeVarCollector::operator()(const Liter& /*liter*/) const
{
}


void ExprRangeVarCollector::operator()(const MultiField& multi_field) const
{
    BOOST_FOREACH(const Quant::RangeVars* rvs_ptr, quant_rvs_stack_) {
        BOOST_FOREACH(const RangeVar& bound_rv, *rvs_ptr)
            if (bound_rv == multi_field.rv)
                return;
    }
    rvs_.add_unsure(multi_field.rv);
}


void ExprRangeVarCollector::operator()(const PosArg& /*pos_arg*/) const
{
}


void ExprRangeVarCollector::operator()(const Quant& quant)
{
    quant_rvs_stack_.push_back(&quant.rvs);
    apply_visitor(*this, quant.pred);
    quant_rvs_stack_.pop_back();
}


void ExprRangeVarCollector::operator()(const Binary& binary)
{
    apply_visitor(*this, binary.left);
    apply_visitor(*this, binary.right);
}


void ExprRangeVarCollector::operator()(const Unary& unary)
{
    apply_visitor(*this, unary.operand);
}


void ExprRangeVarCollector::operator()(const Cond& cond)
{
    apply_visitor(*this, cond.term);
    apply_visitor(*this, cond.yes);
    apply_visitor(*this, cond.no);
}

////////////////////////////////////////////////////////////////////////////////
// ProtoRangeVarCollector
////////////////////////////////////////////////////////////////////////////////

namespace
{
    // Collect unbound rangevars from prototypes
    class ProtoRangeVarCollector : public static_visitor<void> {
    public:
        ProtoRangeVarCollector(RangeVarSet& rvs)
            : rvs_(rvs) {}

        void operator()(const RangeVar& rv) const;
        void operator()(const MultiField& multi_field) const;
        void operator()(const NamedExpr& ne) const;

    private:
        RangeVarSet& rvs_;
    };
}


void ProtoRangeVarCollector::operator()(const RangeVar& rv) const
{
    rvs_.add_unsure(rv);
}


void ProtoRangeVarCollector::operator()(const MultiField& multi_field) const
{
    rvs_.add_unsure(multi_field.rv);
}


void ProtoRangeVarCollector::operator()(const NamedExpr& ne) const
{
    ExprRangeVarCollector expr_rv_collector(rvs_);
    apply_visitor(expr_rv_collector, ne.expr);
}

////////////////////////////////////////////////////////////////////////////////
// ProtoTranslator
////////////////////////////////////////////////////////////////////////////////

namespace
{
    // Print header of SELECT and collect Header of the relation.
    class ProtoTranslator : public static_visitor<void> {
    public:
        ProtoTranslator(Control& control)
            : control_(control) {}

        void operator()(const RangeVar& rv);
        void operator()(const MultiField& multi_field);
        void operator()(const NamedExpr& ne);

        const Header& GetResult() const;

    private:
        Control& control_;
        Header header_;

        void AddAttr(const Attr& attr);
    };
}


void ProtoTranslator::operator()(const RangeVar& rv)
{
    const Header& rv_header(control_.LookupBind(rv));
    BOOST_FOREACH(const Attr& attr, rv_header)
        AddAttr(attr);

    control_ << Quoted(rv.GetName()) << ".*";
}


void ProtoTranslator::operator()(const MultiField& multi_field)
{
    AK_ASSERT(!multi_field.rv.GetName().empty());
    OmitInvoker print_sep((SepPrinter(control_)));
    if (multi_field.IsForeign()) {
        BOOST_FOREACH(const string& field_name, multi_field.path.back()) {
            print_sep();
            MultiField::Path unipath(multi_field.path.begin(),
                                     multi_field.path.end() - 1);
            StringSet unipath_tail;
            unipath_tail.add_sure(field_name);
            unipath.push_back(unipath_tail);
            (*this)(NamedExpr(field_name,
                              MultiField(multi_field.rv, unipath)));
        }
    } else {
        const Header& rv_header(control_.LookupBind(multi_field.rv));
        BOOST_FOREACH(const string& field_name, multi_field.path.back()) {
            print_sep();
            control_ << Quoted(multi_field.rv.GetName())
                     << '.'
                     << Quoted(field_name);

            AddAttr(Attr(field_name, GetAttrType(rv_header, field_name)));
        }
    }
}


void ProtoTranslator::operator()(const NamedExpr& ne)
{
    Type type = control_.TranslateExpr(ne.expr, 0);
    control_ << " AS " << Quoted(ne.name);
    AddAttr(Attr(ne.name, type));
}


const Header& ProtoTranslator::GetResult() const
{
    return header_;
}


void ProtoTranslator::AddAttr(const Attr& attr)
{
    if (!header_.add_unsure(attr))
        throw Error(
            Error::QUERY,
            "Attribute with name \"" + attr.GetName() +"\" appeared twice");
}

////////////////////////////////////////////////////////////////////////////////
// SelectBuilder
////////////////////////////////////////////////////////////////////////////////

namespace
{
    // Build SELECT part by part. Used for translating Quant and Select.
    class SelectBuilder {
    public:
        SelectBuilder(Control& control);
        RangeVarSet CollectRangeVars(const Select::Protos& protos) const;
        Control::BindData BuildFrom(const RangeVarSet& rvs) const;
        Header BuildHeader(const Select::Protos& protos) const;
        void BuildWhere(const Expr& expr, const RangeVar* this_rv_ptr) const;

    private:
        Control& control_;
    };
}


SelectBuilder::SelectBuilder(Control& control)
    : control_(control)
{
    control << "SELECT DISTINCT ";
}


RangeVarSet SelectBuilder::CollectRangeVars(const Select::Protos& protos) const
{
    RangeVarSet result;
    ProtoRangeVarCollector proto_rv_collector(result);
    BOOST_FOREACH(const Proto& proto, protos)
        apply_visitor(proto_rv_collector, proto);
    return result;
}


Control::BindData SelectBuilder::BuildFrom(const RangeVarSet& rvs) const
{
    if (rvs.empty())
        return Control::BindData();

    Control::BindData bind_data;
    bind_data.reserve(rvs.size());
    control_ << " FROM ";
    OmitInvoker print_sep((SepPrinter(control_)));
    BOOST_FOREACH(const RangeVar& rv, rvs) {
        print_sep();
        const Base* base_ptr = boost::get<Base>(&rv.GetRel());
        if (!base_ptr)
            control_ << '(';
        Header header = control_.TranslateRel(rv.GetRel());
        bind_data.push_back(Control::BindUnit(rv, header));
        if (base_ptr)
            AK_ASSERT_EQUAL(base_ptr->name, rv.GetName());
        else
            control_ << ") AS " << Quoted(rv.GetName());
    }
    return bind_data;
}


Header SelectBuilder::BuildHeader(const Select::Protos& protos) const
{
    ProtoTranslator proto_translator(control_);
    OmitInvoker print_sep((SepPrinter(control_)));
    BOOST_FOREACH(const Proto& proto, protos) {
        print_sep();
        apply_visitor(proto_translator, proto);
    }
    return proto_translator.GetResult();
}


void SelectBuilder::BuildWhere(const Expr& expr,
                               const RangeVar* this_rv_ptr) const
{
    if (const Liter* liter_ptr = boost::get<Liter>(&expr)) {
        double d = 0;
        string s;
        liter_ptr->value.Get(d, s);
        if (d || !s.empty())
            return;
    }
    control_ << " WHERE ";
    control_.TranslateExpr(expr, this_rv_ptr, Type::BOOL);
}

////////////////////////////////////////////////////////////////////////////////
// FieldTranslator definitons
////////////////////////////////////////////////////////////////////////////////

namespace
{
    // MultiField expression translator.
    // Translates only MultiFields in fact being unifields
    class FieldTranslator {
    public:
        FieldTranslator(Control& control,
                        const MultiField& multi_field,
                        const RangeVar* this_rv_ptr);
        Type operator()();

    private:
        Control& control_;
        const MultiField& multi_field_;
        const RangeVar* this_rv_ptr_;
        ostringstream from_oss_, where_oss_;
        OmitInvoker print_from_sep_, print_where_sep_;

        string GetFieldName() const;
        const RangeVar& GetRangeVar() const;
        Type TranslateForeignField();
        Type TranslateSelfField() const;
        string FollowReference(const string& key_rel_var_name,
                               const StringSet& key_attr_names);
    };
}


FieldTranslator::FieldTranslator(Control& control,
                                 const MultiField& multi_field,
                                 const RangeVar* this_rv_ptr)
    : control_(control)
    , multi_field_(multi_field)
    , this_rv_ptr_(this_rv_ptr)
    , print_from_sep_(SepPrinter(from_oss_))
    , print_where_sep_(SepPrinter(where_oss_, " AND "))
{
    if (multi_field_.IsMulti())
        throw Error(Error::QUERY, "Multifield used as an expression");
}


Type FieldTranslator::operator()()
{
    return (multi_field_.IsForeign()
            ? TranslateForeignField()
            : TranslateSelfField());
}


string FieldTranslator::GetFieldName() const
{
    AK_ASSERT(!multi_field_.IsMulti());
    return multi_field_.path.back().front();
}


const RangeVar& FieldTranslator::GetRangeVar() const
{
    if (!multi_field_.rv.GetName().empty())
        return multi_field_.rv;
    if (this_rv_ptr_)
        return *this_rv_ptr_;
    throw Error(
        Error::QUERY,
        "No 'this' rangevar in context of a field \"" + GetFieldName() + '"');
}


Type FieldTranslator::TranslateForeignField()
{
    const Base* base_ptr = boost::get<Base>(&GetRangeVar().GetRel());
    if (!base_ptr)
        throw Error(Error::QUERY,
                    ("Operator -> used on non-RelVar rangevar \"" +
                     GetRangeVar().GetName() + '"'));
    string curr_rel_var_name(base_ptr->name);
    for (MultiField::Path::const_iterator itr = multi_field_.path.begin();
         itr != multi_field_.path.end() - 1;
         ++itr)
        curr_rel_var_name = FollowReference(curr_rel_var_name, *itr);
    control_ << "(SELECT "
             << Quoted(curr_rel_var_name) << '.' << Quoted(GetFieldName())
             << " FROM " << from_oss_.str()
             << " WHERE " << where_oss_.str()
             << ')';
    return GetAttrType(get_header_cb(curr_rel_var_name), GetFieldName());
}


Type FieldTranslator::TranslateSelfField() const
{
    control_ << Quoted(GetRangeVar().GetName())
             << '.'
             << Quoted(GetFieldName());
    return GetAttrType(control_.LookupBind(GetRangeVar()), GetFieldName());
}


string FieldTranslator::FollowReference(const string& key_rel_var_name,
                                        const StringSet& key_attr_names)
{
    if (key_rel_var_name == THIS_NAME)
       throw Error(
           Error::QUERY,
           "Operator -> can not be used on fields of an order expr");
    string ref_rel_var_name;
    StringSet ref_attr_names;
    follow_reference_cb(key_rel_var_name, key_attr_names,
                        ref_rel_var_name, ref_attr_names);
    AK_ASSERT_EQUAL(ref_attr_names.size(), key_attr_names.size());

    print_from_sep_();
    from_oss_ << Quoted(ref_rel_var_name);
    for (size_t i = 0; i < key_attr_names.size(); ++i) {
        print_where_sep_();
        where_oss_
            << Quoted(key_rel_var_name) << '.' << Quoted(key_attr_names[i])
            << " = "
            << Quoted(ref_rel_var_name) << '.' << Quoted(ref_attr_names[i]);
    }

    return ref_rel_var_name;
}

////////////////////////////////////////////////////////////////////////////////
// ExprTranslator definitons
////////////////////////////////////////////////////////////////////////////////

ExprTranslator::ExprTranslator(Control& control, const RangeVar* this_rv_ptr)
    : control_(control), this_rv_ptr_(this_rv_ptr)
{
}


Type ExprTranslator::operator()(const Liter& liter) const
{
    control_ << liter.value.GetPgLiter();
    return liter.value.GetType();
}


Type ExprTranslator::operator()(const MultiField& multi_field) const
{
    return FieldTranslator(control_, multi_field, this_rv_ptr_)();
}


Type ExprTranslator::operator()(const PosArg& pos_arg) const
{
    return control_.PrintParam(pos_arg.pos);
}


Type ExprTranslator::operator()(const Quant& quant) const
{
    AK_ASSERT(!quant.rvs.empty());
    string modificator(quant.flag ? "NOT " : "");
    control_ << '(' << modificator << "EXISTS (";
    SelectBuilder builder(control_);
    control_ << '1';
    RangeVarSet rv_set(quant.rvs.begin(), quant.rvs.end());
    Control::BindScope bind_scope(control_, builder.BuildFrom(rv_set));
    control_ << " WHERE " << modificator;
    const RangeVar* this_rv_ptr = (quant.rvs.size() == 1
                                   ? &quant.rvs.front()
                                   : 0);
    control_.TranslateExpr(quant.pred, this_rv_ptr, Type::BOOL);
    control_ << "))";
    return Type::BOOL;
}


Type ExprTranslator::operator()(const Binary& binary) const
{
    string left_str, right_str;
    Type left_type, right_type;
    {
        Control::StringScope string_scope(control_, left_str);
        left_type = apply_visitor(*this, binary.left);
    }
    {
        Control::StringScope string_scope(control_, right_str);
        right_type = apply_visitor(*this, binary.right);
    }
    Type common_type = binary.op.GetCommonType(left_type, right_type);
    control_ << '(' << Cast(left_str, left_type, common_type)
             << ' ' << binary.op.GetPgName(common_type)
             << ' ' << Cast(right_str, right_type, common_type)
             << ')';
    return binary.op.GetResultType(common_type);
}


Type ExprTranslator::operator()(const Unary& unary) const
{
    control_ << unary.op.GetPgName() << ' ';
    control_.TranslateExpr(unary.operand, this_rv_ptr_, unary.op.GetOpType());
    return unary.op.GetOpType();
}


Type ExprTranslator::operator()(const Cond& cond) const
{
    control_ << "(CASE WHEN ";
    control_.TranslateExpr(cond.term, this_rv_ptr_, Type::BOOL);
    string yes_str, no_str;
    Type yes_type, no_type;
    {
        Control::StringScope string_scope(control_, yes_str);
        yes_type = apply_visitor(*this, cond.yes);
    }
    {
        Control::StringScope string_scope(control_, no_str);
        no_type = apply_visitor(*this, cond.no);
    }
    Type common_type;
    if (yes_type == no_type)
        common_type = yes_type;
    else if (yes_type == Type::STRING || no_type == Type::STRING)
        common_type = Type::STRING;
    else
        common_type = Type::NUMBER;

    control_ << " THEN " << Cast(yes_str, yes_type, common_type)
             << " ELSE " << Cast(no_str, no_type, common_type)
             << " END)";
    return common_type;
}

////////////////////////////////////////////////////////////////////////////////
// RelTranslator definitions
////////////////////////////////////////////////////////////////////////////////

Header RelTranslator::operator()(const Base& base) const
{
    control_ << Quoted(base.name);
    return get_header_cb(base.name);
}


Header RelTranslator::operator()(const Select& select) const
{
    SelectBuilder builder(control_);
    if (select.protos.empty()) {
        control_ << '1';
        builder.BuildWhere(select.expr, 0);
        return Header();
    }
    RangeVarSet rvs = builder.CollectRangeVars(select.protos);
    string from_part;
    Control::BindData bind_data;
    {
        Control::StringScope string_scope(control_, from_part);
        bind_data = builder.BuildFrom(rvs);
    }
    Control::BindScope bind_scope(control_, bind_data);
    Header header = builder.BuildHeader(select.protos);
    control_ << from_part;
    const RangeVar* this_rv_ptr = 0;
    if (select.protos.size() == 1) {
        const Proto& proto(select.protos.front());
        if (const RangeVar* rv_ptr = boost::get<RangeVar>(&proto))
            this_rv_ptr = rv_ptr;
        else if (const MultiField* mf_ptr = boost::get<MultiField>(&proto))
            this_rv_ptr = &mf_ptr->rv;
    }
    builder.BuildWhere(select.expr, this_rv_ptr);
    return header;
}


Header RelTranslator::operator()(const Union& un) const
{
    Header left_header = control_.TranslateRel(un.left);
    control_ << " UNION ";
    Header right_header = control_.TranslateRel(un.right);
    if (left_header != right_header)
        throw Error(Error::QUERY,
                    ("Union headers " +
                     lexical_cast<string>(left_header) +
                     " and " +
                     lexical_cast<string>(right_header) +
                     " does not match"));
    return left_header;
}

////////////////////////////////////////////////////////////////////////////////
// API
////////////////////////////////////////////////////////////////////////////////

namespace
{
    string DoTranslateQuery(const string& query,
                            const Drafts& params,
                            Header* header_ptr = 0)
    {
        Control control(params);
        Rel rel(ParseRel(query));
        string result;
        Control::StringScope string_scope(control, result);
        const Header& header(control.TranslateRel(rel));
        if (header_ptr)
            *header_ptr = header;
        return result;
    }


    string DoTranslateExpr(const string& base_name,
                           const Header& base_header,
                           const string& expr_str,
                           const Drafts& params,
                           Type required_type = Type::DUMMY)
    {
        Control control(params);
        Expr expr(ParseExpr(expr_str));
        RangeVar rv(base_name, Base(base_name));
        Control::BindData bind_data;
        bind_data.push_back(Control::BindUnit(rv, base_header));
        Control::BindScope bind_scope(control, bind_data);
        string result;
        {
            Control::StringScope string_scope(control, result);
            control.TranslateExpr(expr, &rv, required_type);
        }
        return result;
    }
}


string ak::TranslateQuery(Header& header,
                          const string& query,
                          const Drafts& query_params,
                          const Strings& by_exprs,
                          const Drafts& by_params,
                          size_t start,
                          size_t length)
{
    ostringstream oss;
    if (!by_exprs.empty())
        oss << "SELECT * FROM (";
    oss << DoTranslateQuery(query, query_params, &header);
    if (!by_exprs.empty()) {
        oss << ") AS " << Quoted(THIS_NAME) << " ORDER BY ";
        OmitInvoker print_sep((SepPrinter(oss)));
        BOOST_FOREACH(const string& by_expr, by_exprs) {
            print_sep();
            oss << DoTranslateExpr(THIS_NAME, header, by_expr, by_params);
        }
    }
    if (length != MINUS_ONE)
        oss << " LIMIT " << length;
    if (start)
        oss << " OFFSET " << start;
    return oss.str();
}


string ak::TranslateCount(const string& query_str, const Drafts& params)
{
    return ("SELECT COUNT(*) FROM (" +
            DoTranslateQuery(query_str, params) +
            ") AS " + Quoted(THIS_NAME));
}


string ak::TranslateUpdate(const string& rel_var_name,
                           const string& where,
                           const Drafts& where_params,
                           const StringMap& expr_map,
                           const Drafts& update_params)
{
    if (expr_map.empty())
        throw Error(Error::VALUE, "Empty update field set");
    ostringstream oss;
    oss << "UPDATE " << Quoted(rel_var_name) << " SET ";
    const Header& header(get_header_cb(rel_var_name));
    OmitInvoker print_sep((SepPrinter(oss)));
    BOOST_FOREACH(const StringMap::value_type& named_expr, expr_map) {
        print_sep();
        oss << Quoted(named_expr.first) << " = ";
        oss << DoTranslateExpr(rel_var_name,
                               header,
                               named_expr.second,
                               update_params,
                               GetAttrType(header, named_expr.first));
    }
    oss << " WHERE " << DoTranslateExpr(rel_var_name,
                                        header,
                                        where,
                                        where_params,
                                        Type::BOOL);
    return oss.str();
}


string ak::TranslateDelete(const string& rel_var_name,
                           const string& where,
                           const Drafts& params)
{
    return ("DELETE FROM " +
            Quoted(rel_var_name) +
            " WHERE " +
            DoTranslateExpr(rel_var_name,
                            get_header_cb(rel_var_name),
                            where,
                            params,
                            Type::BOOL));
}


string ak::TranslateExpr(const string& expr_str,
                         const string& rel_var_name,
                         const Header& rel_header)
{
    return DoTranslateExpr(
        rel_var_name, rel_header, expr_str, Drafts(), Type::BOOL);
}


void ak::InitTranslator(GetHeaderCallback get_header_cb,
                        FollowReferenceCallback follow_reference_cb)
{
    ::get_header_cb = get_header_cb;
    ::follow_reference_cb = follow_reference_cb;
}
