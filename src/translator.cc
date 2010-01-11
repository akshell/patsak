
// (c) 2008-2010 by Anton Korenyushkin

/// \file translator.cc
/// Ku-to-SQL translator impl

#include "translator.h"
#include "parser.h"
#include "utils.h"

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>


using namespace std;
using namespace ku;
using boost::static_visitor;
using boost::apply_visitor;
using boost::lexical_cast;


////////////////////////////////////////////////////////////////////////////////
// Constants
////////////////////////////////////////////////////////////////////////////////

namespace
{
    const char* THIS_NAME = "@";
}

////////////////////////////////////////////////////////////////////////////////
// Casted
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Class for casting expressions to a particular type if necessary and
    /// outputing them.
    class Casted {
    public:
        Casted(Type from_type, Type to_type, const string& expr);
        friend ostream& operator<<(ostream& os, const Casted& casted);

    private:
        string cast_func_;
        string expr_;
    };

    
    ostream& operator<<(ostream& os, const Casted& casted)
    {
        if (casted.cast_func_.empty())
            os << casted.expr_;
        else
            os << casted.cast_func_ << '(' << casted.expr_ << ')';
        return os;
    }
}


Casted::Casted(Type from_type, Type to_type, const string& expr)
    : cast_func_(from_type == to_type ? "" : to_type.GetCastFunc())
    , expr_(expr)
{
}


////////////////////////////////////////////////////////////////////////////////
// Control, RelTranslator and ExprTranslator Declarations
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Translation control class. One instance per translation.
    /// Manages the translation process,
    /// provides means for output and db access.
    class Control {
    public:
        struct BindUnit {
            RangeVar rv;
            Header header;

            BindUnit(const RangeVar& rv, const Header& header)
                : rv(rv), header(header) {}
        };
        
        typedef vector<BindUnit> BindData;

        /// Scope for binding rangevars.
        /// Must only be stack allocated
        class BindScope {
        public:
            BindScope(Control& control, const BindData& bind_data);
            ~BindScope();

        private:
            Control& control_;
        };

        /// Scope for catching output.
        /// Must only be stack allocated
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
        
        Control(const DBViewer& db_viewer,
                const TranslateItem& item,
                const string& rel_var_name = "");

        const Header& LookupBind(const RangeVar& rv) const;
        Header TranslateRel(const Rel& rel);
        Type TranslateExpr(const Expr& expr, const RangeVar* this_rv_ptr);
        void TranslateAndCastExpr(const Expr& expr,
                                  const RangeVar* this_rv_ptr,
                                  Type needed_type);
        Type GetParamType(size_t pos) const;
        void PrintParam(size_t pos);
        const Header& GetRelVarHeader(const string& rel_var_name) const;

        DBViewer::RelVarFields
        GetReference(const DBViewer::RelVarFields& key) const;

        template <typename T>
        Control& operator<<(const T& t);
        Control& operator<<(const PgLiter& pg_liter);
        operator ostream&() const;

    private:
        typedef vector<BindData> BindStack;

        const DBViewer& db_viewer_;
        const TranslateItem& item_;
        const string rel_var_name_;
        BindStack bind_stack_;
        ostream* os_ptr_;

        void CheckParam(size_t pos) const;
    };


    /// Relation translator visitor
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

    
    /// Expression translator visitor
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

Control::Control(const DBViewer& db_viewer,
                 const TranslateItem& item,
                 const string& rel_var_name)
    : db_viewer_(db_viewer)
    , item_(item)
    , rel_var_name_(rel_var_name)
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


Type Control::TranslateExpr(const Expr& expr, const RangeVar* this_rv_ptr)
{
    return apply_visitor(ExprTranslator(*this, this_rv_ptr), expr);    
}


void Control::TranslateAndCastExpr(const Expr& expr,
                                   const RangeVar* this_rv_ptr,
                                   Type cast_type)
{
    string expr_str;
    Type type;
    {
        StringScope string_scope(*this, expr_str);
        type = TranslateExpr(expr, this_rv_ptr);
    }
    *this << Casted(type, cast_type, expr_str);
}


Type Control::GetParamType(size_t pos) const
{
    CheckParam(pos);
    return item_.param_types[pos - 1];
}


void Control::PrintParam(size_t pos)
{
    CheckParam(pos);
    if (item_.param_shift == RAW_SHIFT)
        *this << item_.param_strings[pos - 1];
    else
        *this << '$' << (item_.param_shift + pos);
}


const Header& Control::GetRelVarHeader(const string& rel_var_name) const
{
    return db_viewer_.GetRelVarHeader(rel_var_name);
}


DBViewer::RelVarFields
Control::GetReference(const DBViewer::RelVarFields& key) const
{
    if (key.rel_var_name != THIS_NAME)
        return db_viewer_.GetReference(key);
    if (rel_var_name_.empty())
        throw Error(Error::QUERY, "Operator -> used outside of RelVar context");
    return db_viewer_.GetReference(
        DBViewer::RelVarFields(rel_var_name_, key.field_names));
}


Control::operator ostream&() const
{
    KU_ASSERT(os_ptr_);
    return *os_ptr_;
}


template <typename T>
Control& Control::operator<<(const T& t)
{
    static_cast<ostream&>(*this) << t;
    return *this;
}


Control& Control::operator<<(const PgLiter& pg_liter)
{
    return (*this) << db_viewer_.Quote(pg_liter);
}


void Control::CheckParam(size_t pos) const
{
    if (pos == 0)
        throw Error(Error::QUERY, "Position 0 is invalid");
    if (pos > item_.param_types.size())
        throw Error(Error::QUERY,
                    ("Position " +
                     lexical_cast<string>(pos) +
                     " is out of range"));
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
    KU_ASSERT(!control_.bind_stack_.empty());
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


    /// Collect unbound rangevars from expression
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
    /// Collect unbound rangevars from prototypes
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
    /// Print header of SELECT and collect Header of the relation.
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
    KU_ASSERT(!multi_field.rv.GetName().empty());
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
    /// Build SELECT part by part. Used for translating Quant and Select.
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
            KU_ASSERT(base_ptr->name == rv.GetName());
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
    const Liter* liter_ptr = boost::get<Liter>(&expr);
    if (liter_ptr && liter_ptr->value.GetBool())
        return;
    control_ << " WHERE ";
    control_.TranslateAndCastExpr(expr, this_rv_ptr, Type::BOOLEAN);
}       

////////////////////////////////////////////////////////////////////////////////
// FieldTranslator definitons
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// MultiField expression translator.
    /// Translates only MultiFields in fact being unifields
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
        string FollowReference(const string& rel_var_name,
                               const StringSet& field_names);
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
    KU_ASSERT(!multi_field_.IsMulti());
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
    return GetAttrType(control_.GetRelVarHeader(curr_rel_var_name),
                       GetFieldName());
}


Type FieldTranslator::TranslateSelfField() const
{
    control_ << Quoted(GetRangeVar().GetName())
             << '.'
             << Quoted(GetFieldName());
    return GetAttrType(control_.LookupBind(GetRangeVar()), GetFieldName());
}


string FieldTranslator::FollowReference(const string& rel_var_name,
                                        const StringSet& field_names)
{
    DBViewer::RelVarFields key(rel_var_name, field_names);
    DBViewer::RelVarFields ref(control_.GetReference(key));
    KU_ASSERT(ref.field_names.size() == field_names.size());

    print_from_sep_();
    from_oss_ << Quoted(ref.rel_var_name);
    for (size_t i = 0; i < field_names.size(); ++i) {
        print_where_sep_();
        where_oss_ << Quoted(rel_var_name) << '.' << Quoted(field_names[i])
                   << " = "
                   << Quoted(ref.rel_var_name)
                   << '.' << Quoted(ref.field_names[i]);
    }
    
    return ref.rel_var_name;
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
    control_.PrintParam(pos_arg.pos);
    return control_.GetParamType(pos_arg.pos);
}


Type ExprTranslator::operator()(const Quant& quant) const
{
    KU_ASSERT(!quant.rvs.empty());
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
    control_.TranslateAndCastExpr(quant.pred, this_rv_ptr, Type::BOOLEAN);
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
    control_ << '(' << Casted(left_type, common_type, left_str)
             << ' ' << binary.op.GetPgStr(common_type)
             << ' ' << Casted(right_type, common_type, right_str)
             << ')';
    return binary.op.GetResultType(common_type);
}


Type ExprTranslator::operator()(const Unary& unary) const
{
    control_ << unary.op.GetPgStr() << ' ';
    control_.TranslateAndCastExpr(unary.operand,
                                  this_rv_ptr_,
                                  unary.op.GetOpType());
    return unary.op.GetOpType();
}


Type ExprTranslator::operator()(const Cond& cond) const
{
    control_ << "(CASE WHEN ";
    control_.TranslateAndCastExpr(cond.term, this_rv_ptr_, Type::BOOLEAN);
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

    control_ << " THEN " << Casted(yes_type, common_type, yes_str)
             << " ELSE " << Casted(no_type, common_type, no_str)
             << " END)";
    return common_type;
}

////////////////////////////////////////////////////////////////////////////////
// RelTranslator definitions
////////////////////////////////////////////////////////////////////////////////

Header RelTranslator::operator()(const Base& base) const
{
    control_ << Quoted(base.name);
    return control_.GetRelVarHeader(base.name);
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
// Translator
////////////////////////////////////////////////////////////////////////////////

Translator::Translator(const DBViewer& db_viewer)
    : db_viewer_(db_viewer)
{
}


namespace
{
    string GetRelVarName(const Rel& rel)
    {
        const Select* select_ptr = boost::get<Select>(&rel);
        if (!select_ptr || select_ptr->protos.size() != 1)
            return "";
        const RangeVar* range_var_ptr =
            boost::get<RangeVar>(&select_ptr->protos[0]);
        if (!range_var_ptr)
            return "";
        const Base* base_ptr = boost::get<Base>(&range_var_ptr->GetRel());
        return base_ptr ? base_ptr->name : "";
    }

    
    Translation TranslateBaseRel(const DBViewer& db_viewer,
                                 const TranslateItem& query_item,
                                 string& rel_var_name)
    {
        Control control(db_viewer, query_item);
        Rel rel(ParseRel(query_item.ku_str));
        rel_var_name = GetRelVarName(rel);
        string sql_str;
        Header header;
        {
            Control::StringScope string_scope(control, sql_str);
            header = control.TranslateRel(rel);
        }
        return Translation(sql_str, header);
    }


    string TranslateExpr(const DBViewer& db_viewer,
                         const string& rel_var_name,
                         const string& base_name,
                         const Header& base_header,
                         const TranslateItem& expr_item,
                         Type required_type)
    {
        Control control(db_viewer, expr_item, rel_var_name);
        Expr expr(ParseExpr(expr_item.ku_str));
        RangeVar rv(base_name, Base(base_name));
        Control::BindData bind_data;
        bind_data.push_back(Control::BindUnit(rv, base_header));
        Control::BindScope bind_scope(control, bind_data);
        string result;
        {
            Control::StringScope string_scope(control, result);
            if (required_type != Type::DUMMY)
                control.TranslateAndCastExpr(expr, &rv, required_type);
            else
                control.TranslateExpr(expr, &rv);
        }
        return result;
    }


    void PrintOnlyFields(ostream& os, const StringSet& only_fields)
    {
        if (only_fields.empty()) {
            os << '1';
        } else {
            OmitInvoker print_sep((SepPrinter(os)));
            BOOST_FOREACH(const string& field, only_fields) {
                print_sep();
                os << Quoted(field);
            }
        }
    }


    void PrintExprItems(ostream& os,
                        const DBViewer& db_viewer,
                        const string& rel_var_name,
                        const string& base_name,
                        const Header& base_header,
                        const TranslateItems& expr_items,
                        Type required_type = Type::DUMMY,
                        const string& delimiter = ", ")
    {
        OmitInvoker print_sep((SepPrinter(os, delimiter)));
        BOOST_FOREACH(const TranslateItem& expr_item, expr_items) {
            print_sep();
            os << TranslateExpr(db_viewer, rel_var_name, base_name,
                                base_header, expr_item, required_type);
        }        
    }


    void PrintWhereItems(ostream& os,
                         const DBViewer& db_viewer,
                         const string& rel_var_name,
                         const string& base_name,
                         const Header& base_header,
                         const TranslateItems& where_items)
    {
        if (!where_items.empty()) {
            os << " WHERE ";
            PrintExprItems(os, db_viewer, rel_var_name, base_name, base_header,
                           where_items, Type::BOOLEAN, " AND ");
        }
    }


    void PrintWindow(ostream& os, const Window* window_ptr)
    {
        if (!window_ptr)
            return;
        os << " LIMIT ";
        size_t param_shift = window_ptr->param_shift;
        if (param_shift == RAW_SHIFT)
            os << window_ptr->limit;
        else
            os << '$' << param_shift + 1;
        os << " OFFSET ";
        if (param_shift == RAW_SHIFT)
            os << window_ptr->offset;
        else
            os << '$' << param_shift + 2;
    }
}


Translation Translator::TranslateQuery(const TranslateItem& query_item,
                                       const TranslateItems& where_items,
                                       const TranslateItems& by_items,
                                       const StringSet* only_fields_ptr,
                                       const Window* window_ptr) const
{
    string rel_var_name;
    Translation base_translation(
        TranslateBaseRel(db_viewer_, query_item, rel_var_name));
    if (where_items.empty() && by_items.empty() &&
        !only_fields_ptr && !window_ptr)
        return base_translation;

    const Header& base_header(base_translation.header);

    string by_string;
    if (!by_items.empty()) {
        ostringstream by_oss;
        PrintExprItems(
            by_oss, db_viewer_, rel_var_name, THIS_NAME, base_header, by_items);
        by_string = by_oss.str();
    }
    
    Header header;
    ostringstream oss;
    oss << "SELECT ";
    if (only_fields_ptr) {        
        oss << "DISTINCT ";
        if (!by_items.empty())
            oss << " ON (" << by_string << ") ";
        PrintOnlyFields(oss, *only_fields_ptr);
        BOOST_FOREACH(const string& field, *only_fields_ptr)
            header.add_sure(Attr(field, GetAttrType(base_header, field)));
    } else {
        oss << '*';
        header = base_header;
    }
    
    oss << " FROM (" << base_translation.sql_str
        << ") AS " << Quoted(THIS_NAME);

    PrintWhereItems(
        oss, db_viewer_, rel_var_name, THIS_NAME, base_header, where_items);
    
    if (!by_items.empty())
        oss << " ORDER BY " << by_string;

    PrintWindow(oss, window_ptr);
    
    return Translation(oss.str(), header);
}


string Translator::TranslateCount(const TranslateItem& query_item,
                                  const TranslateItems& where_items,
                                  const Window* window_ptr) const
{
    string rel_var_name;
    Translation base_translation(
        TranslateBaseRel(db_viewer_, query_item, rel_var_name));
    ostringstream oss;
    oss << "SELECT COUNT(*) FROM (" << base_translation.sql_str;
    PrintWindow(oss, window_ptr);
    oss << ") AS " << Quoted(THIS_NAME);
    PrintWhereItems(
        oss, db_viewer_,
        rel_var_name, THIS_NAME, base_translation.header, where_items);
    return oss.str();
}


string Translator::TranslateUpdate(const TranslateItem& update_item,
                                   const StringMap& field_expr_map,
                                   const TranslateItems& where_items) const
{
    if (field_expr_map.empty())
        throw Error(Error::USAGE, "Empty update field set");
    const string& rel_var_name(update_item.ku_str);
    ostringstream oss;
    oss << "UPDATE " << Quoted(rel_var_name) << " SET ";
    const Header& header(db_viewer_.GetRelVarHeader(rel_var_name));

    OmitInvoker print_sep((SepPrinter(oss)));
    BOOST_FOREACH(const StringMap::value_type& field_expr, field_expr_map) {
        print_sep();
        oss << Quoted(field_expr.first) << " = ";
        oss << ::TranslateExpr(db_viewer_,
                               "",
                               rel_var_name,
                               header,
                               (update_item.param_shift == RAW_SHIFT
                                ? TranslateItem(field_expr.second,
                                                update_item.param_types,
                                                update_item.param_strings)
                                : TranslateItem(field_expr.second,
                                                update_item.param_types,
                                                update_item.param_shift)),
                               GetAttrType(header, field_expr.first));
    }
    PrintWhereItems(oss, db_viewer_, "", rel_var_name, header, where_items);
    return oss.str();
}


string Translator::TranslateDelete(const string& rel_var_name,
                                   const TranslateItems& where_items) const
{
    ostringstream oss;
    oss << "DELETE FROM " << Quoted(rel_var_name);
    const Header& header(db_viewer_.GetRelVarHeader(rel_var_name));
    if (!where_items.empty()) {
        oss << " WHERE ";
        PrintExprItems(oss, db_viewer_, "", rel_var_name, header, where_items,
                       Type::BOOLEAN, " AND ");
    }
    return oss.str();
}


string Translator::TranslateExpr(const string& ku_expr_str,
                                 const string& rel_var_name,
                                 const Header& rel_header) const
{
    return ::TranslateExpr(db_viewer_,
                           "",
                           rel_var_name,
                           rel_header,
                           TranslateItem(ku_expr_str),
                           Type::BOOLEAN);
}
