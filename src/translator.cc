// (c) 2008-2010 by Anton Korenyushkin

#include "translator.h"
#include "parser.h"

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

        template <typename T> Control& operator<<(const T& t);
        template <typename T> Control& operator<<(T& t);

    private:
        typedef vector<BindData> BindStack;

        const Drafts& params_;
        BindStack bind_stack_;
        ostream* os_ptr_;
    };


    class RelTranslator : public static_visitor<Header> {
    public:
        RelTranslator(Control& control);

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
    *this << value;
    return value.GetType();
}


template <typename T>
Control& Control::operator<<(const T& t)
{
    AK_ASSERT(os_ptr_);
    *os_ptr_ << t;
    return *this;
}


template <typename T>
Control& Control::operator<<(T& t)
{
    AK_ASSERT(os_ptr_);
    *os_ptr_ << t;
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
    // Collect unbound rangevars from expression
    class ExprRangeVarCollector : public static_visitor<void> {
    public:
        ExprRangeVarCollector(RangeVarSet& rv_set);

        void operator()(const Liter& liter) const;
        void operator()(const MultiField& multi_field) const;
        void operator()(const PosArg& pos_arg) const;
        void operator()(const Quant& quant);
        void operator()(const Binary& binary);
        void operator()(const Unary& unary);
        void operator()(const Cond& cond);

    private:
        RangeVarSet& rv_set_;
        vector<const RangeVarSet*> quant_rv_set_stack_;
    };
}


ExprRangeVarCollector::ExprRangeVarCollector(RangeVarSet& rv_set)
    : rv_set_(rv_set)
{
}


void ExprRangeVarCollector::operator()(const Liter& /*liter*/) const
{
}


void ExprRangeVarCollector::operator()(const MultiField& multi_field) const
{
    BOOST_FOREACH(const RangeVarSet* rv_set_ptr, quant_rv_set_stack_) {
        BOOST_FOREACH(const RangeVar& bound_rv, *rv_set_ptr)
            if (bound_rv == multi_field.rv)
                return;
    }
    rv_set_.add_safely(multi_field.rv);
}


void ExprRangeVarCollector::operator()(const PosArg& /*pos_arg*/) const
{
}


void ExprRangeVarCollector::operator()(const Quant& quant)
{
    quant_rv_set_stack_.push_back(&quant.rv_set);
    apply_visitor(*this, quant.pred);
    quant_rv_set_stack_.pop_back();
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
        ProtoRangeVarCollector(RangeVarSet& rv_set);

        void operator()(const RangeVar& rv) const;
        void operator()(const MultiField& multi_field) const;
        void operator()(const NamedExpr& ne) const;

    private:
        RangeVarSet& rv_set_;
    };
}


ProtoRangeVarCollector::ProtoRangeVarCollector(RangeVarSet& rv_set)
    : rv_set_(rv_set)
{
}


void ProtoRangeVarCollector::operator()(const RangeVar& rv) const
{
    rv_set_.add_safely(rv);
}


void ProtoRangeVarCollector::operator()(const MultiField& multi_field) const
{
    rv_set_.add_safely(multi_field.rv);
}


void ProtoRangeVarCollector::operator()(const NamedExpr& ne) const
{
    ExprRangeVarCollector expr_rv_collector(rv_set_);
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

    control_ << '"' << rv.GetName() << "\".*";
}


void ProtoTranslator::operator()(const MultiField& multi_field)
{
    AK_ASSERT(!multi_field.rv.GetName().empty());
    Separator sep;
    if (multi_field.IsForeign()) {
        BOOST_FOREACH(const string& field_name, multi_field.path.back()) {
            control_ << sep;
            MultiField::Path unipath(multi_field.path.begin(),
                                     multi_field.path.end() - 1);
            StringSet unipath_tail;
            unipath_tail.add(field_name);
            unipath.push_back(unipath_tail);
            (*this)(NamedExpr(field_name,
                              MultiField(multi_field.rv, unipath)));
        }
    } else {
        const Header& rv_header(control_.LookupBind(multi_field.rv));
        BOOST_FOREACH(const string& field_name, multi_field.path.back()) {
            control_ << sep << '"' << multi_field.rv.GetName()
                     << "\".\"" << field_name << '"';
            AddAttr(Attr(field_name, GetAttr(rv_header, field_name).type));
        }
    }
}


void ProtoTranslator::operator()(const NamedExpr& ne)
{
    Type type = control_.TranslateExpr(ne.expr, 0);
    control_ << " AS \"" << ne.name << '"';
    AddAttr(Attr(ne.name, type));
}


const Header& ProtoTranslator::GetResult() const
{
    return header_;
}


void ProtoTranslator::AddAttr(const Attr& attr)
{
    if (!header_.add_safely(attr))
        throw Error(
            Error::QUERY,
            "Attribute with name \"" + attr.name +"\" appeared twice");
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
        RangeVarSet CollectRangeVars(const Protos& protos) const;
        Control::BindData BuildFrom(const RangeVarSet& rv_set) const;
        Header BuildHeader(const Protos& protos) const;
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


RangeVarSet SelectBuilder::CollectRangeVars(const Protos& protos) const
{
    RangeVarSet result;
    ProtoRangeVarCollector proto_rv_collector(result);
    BOOST_FOREACH(const Proto& proto, protos)
        apply_visitor(proto_rv_collector, proto);
    return result;
}


Control::BindData SelectBuilder::BuildFrom(const RangeVarSet& rv_set) const
{
    if (rv_set.empty())
        return Control::BindData();

    Control::BindData bind_data;
    bind_data.reserve(rv_set.size());
    control_ << " FROM ";
    Separator sep;
    BOOST_FOREACH(const RangeVar& rv, rv_set) {
        control_ << sep;
        const Base* base_ptr = boost::get<Base>(&rv.GetRel());
        if (!base_ptr)
            control_ << '(';
        Header header = control_.TranslateRel(rv.GetRel());
        bind_data.push_back(Control::BindUnit(rv, header));
        if (base_ptr)
            AK_ASSERT_EQUAL(base_ptr->name, rv.GetName());
        else
            control_ << ") AS \"" << rv.GetName() << '"';
    }
    return bind_data;
}


Header SelectBuilder::BuildHeader(const Protos& protos) const
{
    ProtoTranslator proto_translator(control_);
    Separator sep;
    BOOST_FOREACH(const Proto& proto, protos) {
        control_ << sep;
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
    control_.TranslateExpr(expr, this_rv_ptr, Type::BOOLEAN);
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
        Separator from_sep_, where_sep_;

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
    , where_sep_(" AND ")
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
    control_ << "(SELECT \""
             << curr_rel_var_name << "\".\"" << GetFieldName()
             << "\" FROM " << from_oss_.str()
             << " WHERE " << where_oss_.str()
             << ')';
    return GetAttr(get_header_cb(curr_rel_var_name), GetFieldName()).type;
}


Type FieldTranslator::TranslateSelfField() const
{
    control_ << '"' << GetRangeVar().GetName()
             << "\".\"" << GetFieldName() << '"';
    return GetAttr(control_.LookupBind(GetRangeVar()), GetFieldName()).type;
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

    from_oss_ << from_sep_ << '"' << ref_rel_var_name << '"';
    for (size_t i = 0; i < key_attr_names.size(); ++i)
        where_oss_ << where_sep_
                   << '"' << key_rel_var_name << "\".\"" << key_attr_names[i]
                   << "\" = \""
                   << ref_rel_var_name << "\".\"" << ref_attr_names[i] << '"';

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
    control_ << liter.value;
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
    AK_ASSERT(!quant.rv_set.empty());
    string modificator(quant.flag ? "NOT " : "");
    control_ << '(' << modificator << "EXISTS (";
    SelectBuilder builder(control_);
    control_ << '1';
    Control::BindScope bind_scope(control_, builder.BuildFrom(quant.rv_set));
    control_ << " WHERE " << modificator;
    const RangeVar* this_rv_ptr = (quant.rv_set.size() == 1
                                   ? &quant.rv_set.front()
                                   : 0);
    control_.TranslateExpr(quant.pred, this_rv_ptr, Type::BOOLEAN);
    control_ << "))";
    return Type::BOOLEAN;
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
    control_.TranslateExpr(cond.term, this_rv_ptr_, Type::BOOLEAN);
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

RelTranslator::RelTranslator(Control& control)
    : control_(control)
{
}


Header RelTranslator::operator()(const Base& base) const
{
    control_ << '"' << base.name << '"';
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
    RangeVarSet rv_set = builder.CollectRangeVars(select.protos);
    string from_part;
    Control::BindData bind_data;
    {
        Control::StringScope string_scope(control_, from_part);
        bind_data = builder.BuildFrom(rv_set);
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
    if (left_header.size() != right_header.size())
        throw Error(Error::QUERY, "Union headers have different sizes");
    BOOST_FOREACH(const Attr& left_attr, left_header) {
        const Attr* right_attr_ptr = right_header.find(left_attr.name);
        if (!(right_attr_ptr &&
              (left_attr.type == right_attr_ptr->type ||
               (left_attr.type.IsNumeric() &&
                right_attr_ptr->type.IsNumeric()))))
            throw Error(Error::QUERY, "Union headers don't match");
    }
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
        oss << ") AS \"" << THIS_NAME << "\" ORDER BY ";
        Separator sep;
        BOOST_FOREACH(const string& by_expr, by_exprs)
            oss << sep
                << DoTranslateExpr(THIS_NAME, header, by_expr, by_params);
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
            ") AS \"" + THIS_NAME + '"');
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
    oss << "UPDATE \"" << rel_var_name << "\" SET ";
    const Header& header(get_header_cb(rel_var_name));
    Separator sep;
    BOOST_FOREACH(const NamedString& named_expr, expr_map)
        oss << sep << '"' << named_expr.name << "\" = "
            << DoTranslateExpr(rel_var_name,
                               header,
                               named_expr.str,
                               update_params,
                               GetAttr(header, named_expr.name).type);
    oss << " WHERE " << DoTranslateExpr(rel_var_name,
                                        header,
                                        where,
                                        where_params,
                                        Type::BOOLEAN);
    return oss.str();
}


string ak::TranslateDelete(const string& rel_var_name,
                           const string& where,
                           const Drafts& params)
{
    return ("DELETE FROM \"" + rel_var_name + "\" WHERE " +
            DoTranslateExpr(rel_var_name,
                            get_header_cb(rel_var_name),
                            where,
                            params,
                            Type::BOOLEAN));
}


string ak::TranslateExpr(const string& expr_str,
                         const string& rel_var_name,
                         const Header& rel_header)
{
    return DoTranslateExpr(
        rel_var_name, rel_header, expr_str, Drafts(), Type::BOOLEAN);
}


void ak::InitTranslator(GetHeaderCallback get_header_cb,
                        FollowReferenceCallback follow_reference_cb)
{
    ::get_header_cb = get_header_cb;
    ::follow_reference_cb = follow_reference_cb;
}
