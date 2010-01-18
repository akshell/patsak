
// (c) 2009-2010 by Anton Korenyushkin

/// \file js-db.cc
/// JavaScript database stuff

#include "js-db.h"
#include "db.h"

#include <boost/foreach.hpp>
#include <boost/ref.hpp>


using namespace std;
using namespace ku;
using namespace v8;
using boost::ref;
using boost::shared_ptr;


////////////////////////////////////////////////////////////////////////////////
// Constants
////////////////////////////////////////////////////////////////////////////////

namespace
{
#ifdef TEST
    const size_t MEMORY_MULTIPLIER = 1024;
#else
    const size_t MEMORY_MULTIPLIER = 1;
#endif
}

////////////////////////////////////////////////////////////////////////////////
// access_ptr
////////////////////////////////////////////////////////////////////////////////

namespace ku
{
    Access* access_ptr;
}

////////////////////////////////////////////////////////////////////////////////
// Readers
////////////////////////////////////////////////////////////////////////////////

namespace
{
    void CheckIsIdentifier(const string& str)
    {
        if (str.empty())
            throw Error(Error::USAGE, "Identifier can't be empty");
        const locale& loc(locale::classic());
        if (str[0] != '_' && !isalpha(str[0], loc))
            throw Error(Error::USAGE,
                        ("First identifier character must be "
                         "a letter or underscore"));
        for (size_t i = 1; i < str.size(); ++i)
            if (str[i] != '_' && !isalnum(str[i], loc))
                throw Error(Error::USAGE,
                            ("Identifier must consist only of "
                             "letters, digits or underscores"));
    }

    
    ku::Value ReadKuValue(Handle<v8::Value> v8_value)
    {
        if (v8_value->IsBoolean())
            return ku::Value(Type::BOOLEAN, v8_value->BooleanValue());
        if (v8_value->IsNumber())
            return ku::Value(Type::NUMBER, v8_value->NumberValue());
        if (v8_value->IsDate())
            return ku::Value(Type::DATE, v8_value->NumberValue());
        return ku::Value(Type::STRING, Stringify(v8_value));
    }


    Handle<v8::Value> MakeV8Value(const ku::Value& ku_value)
    {
        Type type(ku_value.GetType());
        if (type == Type::NUMBER)
            return Number::New(ku_value.GetDouble());
        else if (type == Type::STRING)
            return String::New(ku_value.GetString().c_str());
        else if (type == Type::BOOLEAN) 
            return Boolean::New(ku_value.GetBool());
        else {
            KU_ASSERT(type == Type::DATE);
            return Date::New(ku_value.GetDouble());
        }
    }


    template <typename ContainerT>
    Handle<Array> MakeV8Array(const ContainerT& container)
    {
        int32_t size = static_cast<int32_t>(container.size());
        Handle<Array> result(Array::New(size));
        for (int32_t i = 0; i < size; ++i)
            result->Set(Integer::New(i), String::New(container[i].c_str()));
        return result;
    }

    
    StringSet ReadStringSet(v8::Handle<v8::Value> value)
    {
        StringSet result;
        int32_t length = GetArrayLikeLength(value);
        if (length == -1) {
            result.add_sure(Stringify(value));
        } else {
            result.reserve(length);
            for (int32_t i = 0; i < length; ++i) {
                Handle<v8::Value> item(GetArrayLikeItem(value, i));
                if (!result.add_unsure(Stringify(item)))
                    throw Error(Error::USAGE, "Duplicating names");
            }
        }
        return result;
    }


    StringSet ReadStringSet(const Arguments& args)
    {
        if (args.Length() == 1)
            return ReadStringSet(args[0]);
        StringSet result;
        result.reserve(args.Length());
        for (int i = 0; i < args.Length(); ++i)
            if (!result.add_unsure(Stringify(args[i])))
                throw Error(Error::USAGE, "Dumplicating names");
        return result;
    }
}    

////////////////////////////////////////////////////////////////////////////////
// ConstrBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class ConstrBg {
    public:
        DECLARE_JS_CLASS(ConstrBg);
        ConstrBg(const Constr& constr);
        const Constr& GetConstr() const;

    private:
        Constr constr_;
    };
}


DEFINE_JS_CLASS(ConstrBg, "Constr", /*object_template*/, /*proto_template*/)
{
    // TODO
}


ConstrBg::ConstrBg(const Constr& constr)
    : constr_(constr)
{
}


const Constr& ConstrBg::GetConstr() const
{
    return constr_;
}

////////////////////////////////////////////////////////////////////////////////
// AddedConstr
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class AddedConstr {
    public:
        virtual Constr GetConstr(const string& field_name) const = 0;
        virtual ~AddedConstr() {}
    };


    class AddedUnique : public AddedConstr {
    public:
        virtual Constr GetConstr(const string& field_name) const {
            StringSet field_names;
            field_names.add_sure(field_name);
            return Unique(field_names);
        }
    };


    class AddedForeignKey : public AddedConstr {
    public:
        AddedForeignKey(const string& ref_rel_var_name,
                        const string& ref_field_name)
            : ref_rel_var_name_(ref_rel_var_name)
            , ref_field_name_(ref_field_name) {}
        
        virtual Constr GetConstr(const string& field_name) const {
            StringSet key_field_names, ref_field_names;
            key_field_names.add_sure(field_name);
            ref_field_names.add_sure(ref_field_name_);
            return ForeignKey(key_field_names,
                              ref_rel_var_name_,
                              ref_field_names);
        }

    private:
        string ref_rel_var_name_;
        string ref_field_name_;
    };


    class AddedCheck : public AddedConstr {
    public:
        AddedCheck(const string& expr_str)
            : check_(expr_str) {}

        virtual Constr GetConstr(const string& /*field_name*/) const {
            return check_;
        }

    private:
        Check check_;
    };
}

////////////////////////////////////////////////////////////////////////////////
// TypeBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// number, string and boolean background
    class TypeBg {
    public:
        DECLARE_JS_CLASS(TypeBg);

        TypeBg(Type type);
        
        Type GetType() const;
        Type::Trait GetTrait() const;
        const ku::Value* GetDefaultPtr() const;
        void CollectConstrs(const string& field_name, Constrs& constrs) const;

    private:
        typedef shared_ptr<AddedConstr> AddedConstrPtr;
        typedef vector<AddedConstrPtr> AddedConstrPtrs;

        Type type_;
        Type::Trait trait_;
        shared_ptr<ku::Value> default_ptr_;
        AddedConstrPtrs ac_ptrs_;

        friend Handle<Object> JSNew<TypeBg>(Type,
                                            Type::Trait,
                                            shared_ptr<ku::Value>,
                                            AddedConstrPtrs);
        
        TypeBg(Type type,
               Type::Trait trait,
               shared_ptr<ku::Value> default_ptr,
               const AddedConstrPtrs& ac_ptrs);        
        
        DECLARE_JS_CALLBACK2(Handle<v8::Value>, GetNameCb,
                             Local<String>, const AccessorInfo&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, IntegerCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, SerialCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, DefaultCb,
                             const Arguments&) const;

        Handle<v8::Value> NewWithAddedConstr(AddedConstrPtr ac_ptr) const;
        
        DECLARE_JS_CALLBACK1(Handle<v8::Value>, UniqueCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, ForeignCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, CheckCb,
                             const Arguments&) const;
    };
}


DEFINE_JS_CLASS(TypeBg, "Type", object_template, proto_template)
{
    object_template->SetAccessor(String::NewSymbol("name"), GetNameCb);
    SetFunction(proto_template, "_integer", IntegerCb);
    SetFunction(proto_template, "_serial", SerialCb);
    SetFunction(proto_template, "_default", DefaultCb);
    SetFunction(proto_template, "_unique", UniqueCb);
    SetFunction(proto_template, "_foreign", ForeignCb);
    SetFunction(proto_template, "_check", CheckCb);
}


TypeBg::TypeBg(Type type)
    : type_(type), trait_(Type::COMMON)
{
}


TypeBg::TypeBg(Type type,
               Type::Trait trait,
               shared_ptr<ku::Value> default_ptr,
               const AddedConstrPtrs& ac_ptrs)
    : type_(type)
    , trait_(trait)
    , default_ptr_(default_ptr)
    , ac_ptrs_(ac_ptrs)
{
}


Type TypeBg::GetType() const
{
    return type_;
}


Type::Trait TypeBg::GetTrait() const
{
    return trait_;
}


const ku::Value* TypeBg::GetDefaultPtr() const
{
    return default_ptr_.get();
}


void TypeBg::CollectConstrs(const string& field_name, Constrs& constrs) const
{
    constrs.reserve(constrs.size() + ac_ptrs_.size());
    BOOST_FOREACH(const AddedConstrPtr& ac_ptr, ac_ptrs_) {
        KU_ASSERT(ac_ptr);
        constrs.push_back(ac_ptr->GetConstr(field_name));
    }
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, TypeBg, GetNameCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/) const
{
    return String::New(type_.GetKuStr().c_str());
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, TypeBg, IntegerCb,
                    const Arguments&, /*args*/) const
{
    if (trait_ != Type::COMMON)
        throw Error(Error::USAGE, "Trait redefinition");
    return JSNew<TypeBg>(type_, Type::INTEGER, default_ptr_, ac_ptrs_);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, TypeBg, SerialCb,
                    const Arguments&, /*args*/) const
{
    if (trait_ != Type::COMMON)
        throw Error(Error::USAGE, "Trait redefinition");
    if (default_ptr_)
        throw Error(Error::USAGE, "Default and serial are incompatible");
    return JSNew<TypeBg>(type_, Type::SERIAL, default_ptr_, ac_ptrs_);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, TypeBg, DefaultCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    if (trait_ == Type::SERIAL)
        throw Error(Error::USAGE, "Default and serial are incompatible");
    shared_ptr<ku::Value> default_ptr(new ku::Value(ReadKuValue(args[0])));
    return JSNew<TypeBg>(type_, trait_, default_ptr, ac_ptrs_);
}


Handle<v8::Value> TypeBg::NewWithAddedConstr(AddedConstrPtr ac_ptr) const
{
    KU_ASSERT(ac_ptr.get());
    AddedConstrPtrs new_ac_ptrs(ac_ptrs_);
    new_ac_ptrs.push_back(ac_ptr);
    return JSNew<TypeBg>(type_, trait_, default_ptr_, new_ac_ptrs);
    
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, TypeBg, UniqueCb,
                    const Arguments&, /*args*/) const
{
    return NewWithAddedConstr(AddedConstrPtr(new AddedUnique()));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, TypeBg, ForeignCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 2);
    AddedConstrPtr ac_ptr(new AddedForeignKey(Stringify(args[0]),
                                              Stringify(args[1])));
    return NewWithAddedConstr(ac_ptr);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, TypeBg, CheckCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    AddedConstrPtr ac_ptr(new AddedCheck(Stringify(args[0])));
    return NewWithAddedConstr(ac_ptr);
}

////////////////////////////////////////////////////////////////////////////////
// RelBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Background of a query result
    class RelBg {
    public:
        DECLARE_JS_CLASS(RelBg);
        
        RelBg(const string& query_str,
              const Values& params,
              const Specs& specs);
        virtual ~RelBg();

    protected:
        static void InitRelObjectTemplate(Handle<ObjectTemplate>);
        string GetQueryStr() const;
        WhereSpecs GetWhereSpecs() const;
        
    private:
        string query_str_;
        Values params_;
        Specs specs_;
        auto_ptr<QueryResult> query_result_ptr_;

        const QueryResult& GetQueryResult();

        DECLARE_JS_CALLBACK2(Handle<v8::Value>, GetLengthCb,
                             Local<String>, const AccessorInfo&);

        DECLARE_JS_CALLBACK2(Handle<v8::Value>, GetTupleCb,
                             uint32_t, const AccessorInfo&);

        static Handle<v8::Value> SetTupleCb(uint32_t index,
                                            Local<v8::Value> value,
                                            const AccessorInfo& info);

        DECLARE_JS_CALLBACK2(Handle<Boolean>, HasTupleCb,
                             uint32_t, const AccessorInfo&);

        DECLARE_JS_CALLBACK1(Handle<Array>, EnumTuplesCb,
                             const AccessorInfo&);

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, PerformCb,
                             const Arguments&);

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, OnlyCb,
                             const Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, SubrelCb,
                             const Arguments&) const;

        template <typename SpecT>
        Handle<v8::Value> GenericSpecify(const Arguments& args) const;

        virtual Handle<v8::Value>
        InstantiateWithSpecs(const Specs& new_specs) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, WhereCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ByCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, CountCb,
                             const Arguments&) const;
    };
}


DEFINE_JS_CLASS(RelBg, "Rel", object_template, proto_template)
{
    InitRelObjectTemplate(object_template);
    SetFunction(proto_template, "_perform", PerformCb);
    SetFunction(proto_template, "_only", OnlyCb);
    SetFunction(proto_template, "_subrel", SubrelCb);
    SetFunction(proto_template, "_where", WhereCb);
    SetFunction(proto_template, "_by", ByCb);
    SetFunction(proto_template, "_count", CountCb);
}


RelBg::RelBg(const string& query_str,
             const Values& params,
             const Specs& specs)
    : query_str_(query_str)
    , params_(params)
    , specs_(specs)
{
}


RelBg::~RelBg()
{
    if (query_result_ptr_.get())
        V8::AdjustAmountOfExternalAllocatedMemory(
            -MEMORY_MULTIPLIER * query_result_ptr_->GetMemoryUsage());
}


void RelBg::InitRelObjectTemplate(Handle<ObjectTemplate> object_template)
{
    object_template->SetAccessor(String::NewSymbol("length"),
                                 &GetLengthCb,
                                 0,
                                 Handle<v8::Value>(),
                                 DEFAULT,
                                 ReadOnly |DontEnum | DontDelete);
    
    object_template->SetIndexedPropertyHandler(GetTupleCb,
                                               SetTupleCb,
                                               HasTupleCb,
                                               0,
                                               EnumTuplesCb);
}


string RelBg::GetQueryStr() const
{
    return query_str_;
}


WhereSpecs RelBg::GetWhereSpecs() const
{
    WhereSpecs result;
    BOOST_FOREACH(const Spec& spec, specs_)
        if (const WhereSpec* where_spec_ptr = boost::get<WhereSpec>(&spec))
            result.push_back(*where_spec_ptr);
    return result;
}


const QueryResult& RelBg::GetQueryResult()
{
    if (!query_result_ptr_.get()) {
        QueryResult query_result(
            access_ptr->Query(query_str_, params_, specs_));
        query_result_ptr_.reset(new QueryResult(query_result));
        V8::AdjustAmountOfExternalAllocatedMemory(
            MEMORY_MULTIPLIER * query_result.GetMemoryUsage());
    }
    return *query_result_ptr_;
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, RelBg, GetLengthCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/)
{
    return Integer::New(GetQueryResult().GetSize());
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, RelBg, GetTupleCb,
                    uint32_t, index,
                    const AccessorInfo&, /*info*/)
{
    auto_ptr<Values> values_ptr(GetQueryResult().GetValuesPtr(index));
    if (!values_ptr.get())
        return Handle<v8::Value>();
    const Header& header(GetQueryResult().GetHeader());
    KU_ASSERT(values_ptr->size() == header.size());
    Handle<Object> result(Object::New());
    for (size_t i = 0; i < header.size(); ++i)
        Set(result, header[i].GetName(), MakeV8Value((*values_ptr)[i]));
    return result;
}


Handle<v8::Value> RelBg::SetTupleCb(uint32_t /*index*/,
                                    Local<v8::Value> value,
                                    const AccessorInfo& /*info*/)
{
    return value;
}


DEFINE_JS_CALLBACK2(Handle<Boolean>, RelBg, HasTupleCb,
                    uint32_t, index,
                    const AccessorInfo&, /*info*/)
{
    // TODO may it's possible to throw
    if (!query_result_ptr_.get())
        return Boolean::New(false);
    return Boolean::New(index < query_result_ptr_->GetSize());
}


DEFINE_JS_CALLBACK1(Handle<Array>, RelBg, EnumTuplesCb,
                    const AccessorInfo&, /*info*/)
{
    // TODO may it's possible to throw
    if (!query_result_ptr_.get())
        return Array::New(0);
    size_t tuples_size = query_result_ptr_->GetSize();
    Handle<Array> result(Array::New(tuples_size));
    for (size_t i = 0; i < tuples_size; ++i)
        result->Set(Integer::New(i), Integer::New(i));
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelBg, PerformCb,
                    const Arguments&, /*args*/)
{
    GetQueryResult();
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelBg, OnlyCb,
                    const Arguments&, args) const
{
    StringSet field_names(ReadStringSet(args));
    Specs new_specs(specs_);
    new_specs.push_back(OnlySpec(field_names));
    return InstantiateWithSpecs(new_specs);
}


namespace
{
    unsigned long ReadUnsigned(Handle<v8::Value> value)
    {
        Handle<Integer> integer(value->ToInteger());
        if (integer.IsEmpty() || integer->Value() < 0)
            throw Error(Error::TYPE, "Unsigned integer required");
        return integer->Value();
    }
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelBg, SubrelCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    Specs new_specs(specs_);
    new_specs.push_back(WindowSpec(ReadUnsigned(args[0]),
                                   (args.Length() > 1
                                    ? ReadUnsigned(args[1])
                                    : WindowSpec::ALL)));
    return InstantiateWithSpecs(new_specs);
}


template <typename SpecT>
Handle<v8::Value> RelBg::GenericSpecify(const Arguments& args) const
{
    CheckArgsLength(args,  1);
    string expr_str = Stringify(args[0]);
    Values params;
    params.reserve(args.Length() - 1);
    for (int i = 1; i < args.Length(); ++i)
        params.push_back(ReadKuValue(args[i]));
    Specs new_specs(specs_);
    new_specs.push_back(SpecT(expr_str, params));
    return InstantiateWithSpecs(new_specs);
}


Handle<v8::Value>
RelBg::InstantiateWithSpecs(const Specs& new_specs) const
{
    return JSNew<RelBg>(query_str_, params_, new_specs);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelBg, WhereCb,
                    const Arguments&, args) const
{
    return GenericSpecify<WhereSpec>(args);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelBg, ByCb,
                    const Arguments&, args) const
{
    return GenericSpecify<BySpec>(args);
}
    
    
DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelBg, CountCb,
                    const Arguments&, /*args*/) const
{
    return Integer::New(access_ptr->Count(query_str_, params_, specs_));
    
}

////////////////////////////////////////////////////////////////////////////////
// SelectionBg and RelVarBg declarations
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class SelectionBg : public RelBg {
    public:
        DECLARE_JS_CLASS(SelectionBg);
        
        SelectionBg(const string& rel_var_name, const Specs& specs);

    private:
        string GetRelVarName() const;
        
        virtual Handle<v8::Value>
        InstantiateWithSpecs(const Specs& new_specs) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, GetRelVarNameCb,
                             const Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, UpdateCb,
                             const Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, DelCb,
                             const Arguments&) const;
    };


    class RelVarBg {
    public:
        DECLARE_JS_CLASS(RelVarBg);

        RelVarBg(const string& name);
        
    private:
        string name_;
        
        const RichHeader& GetRichHeader() const;
        const Constrs& GetConstrs() const;

        DECLARE_JS_CALLBACK2(Handle<v8::Value>, GetNameCb,
                             Local<String>, const AccessorInfo&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, GetHeaderCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, CreateCb,
                             const Arguments&) const;
        
        DECLARE_JS_CALLBACK1(Handle<v8::Value>, InsertCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, DropCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, AllCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, GetIntegersCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, GetSerialsCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, GetDefaultsCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, GetUniquesCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, GetForeignsCb,
                             const Arguments&) const;
    };
}

////////////////////////////////////////////////////////////////////////////////
// SelectionBg definitions
////////////////////////////////////////////////////////////////////////////////

DEFINE_JS_CLASS(SelectionBg, "Selection", object_template, proto_template)
{
    InitRelObjectTemplate(object_template);
    SetFunction(proto_template, "_getRelVarName", GetRelVarNameCb);
    SetFunction(proto_template, "_update", UpdateCb);
    SetFunction(proto_template, "_del", DelCb);
}


SelectionBg::SelectionBg(const string& rel_var_name, const Specs& specs)
    : RelBg(rel_var_name, Values(), specs)
{
}


string SelectionBg::GetRelVarName() const
{
    return GetQueryStr();
}


Handle<v8::Value>
SelectionBg::InstantiateWithSpecs(const Specs& new_specs) const
{
    return JSNew<SelectionBg>(GetRelVarName(), new_specs);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, SelectionBg, GetRelVarNameCb,
                    const Arguments&, /*args*/) const
{
    return String::New(GetRelVarName().c_str());
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, SelectionBg, UpdateCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    if (!args[0]->IsObject())
        throw Error(Error::TYPE, "Update argument must be an object");
    StringMap field_expr_map;
    PropEnumerator prop_enumerator(args[0]->ToObject());
    for (size_t i = 0; i < prop_enumerator.GetSize(); ++i) {
        Prop prop(prop_enumerator.GetProp(i));
        field_expr_map.insert(StringMap::value_type(Stringify(prop.key),
                                                    Stringify(prop.value)));
    }
    Values params;
    params.reserve(args.Length() - 1);
    for (int i = 1; i < args.Length(); ++i)
        params.push_back(ReadKuValue(args[i]));
    unsigned long rows_number = (access_ptr->Update(GetRelVarName(),
                                                    field_expr_map,
                                                    params,
                                                    GetWhereSpecs()));
    return Number::New(static_cast<double>(rows_number));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, SelectionBg, DelCb,
                    const Arguments&, /*args*/) const
{
    unsigned long rows_number = (
        access_ptr->Delete(GetRelVarName(), GetWhereSpecs()));
    return Number::New(static_cast<double>(rows_number));
}

////////////////////////////////////////////////////////////////////////////////
// RelVarBg definitions
////////////////////////////////////////////////////////////////////////////////

DEFINE_JS_CLASS(RelVarBg, "RelVar", object_template, proto_template)
{
    SelectionBg::GetJSClass();
    RelBg::GetJSClass().AddSubClass(SelectionBg::GetJSClass());
    object_template->SetAccessor(String::NewSymbol("name"), GetNameCb);
    SetFunction(proto_template, "_getHeader", GetHeaderCb);
    SetFunction(proto_template, "_create", CreateCb);
    SetFunction(proto_template, "_insert", InsertCb);
    SetFunction(proto_template, "_drop", DropCb);
    SetFunction(proto_template, "_all", AllCb);
    SetFunction(proto_template, "_getIntegers", GetIntegersCb);
    SetFunction(proto_template, "_getSerials", GetSerialsCb);
    SetFunction(proto_template, "_getDefaults", GetDefaultsCb);
    SetFunction(proto_template, "_getUniques", GetUniquesCb);
    SetFunction(proto_template, "_getForeigns", GetForeignsCb);
}


RelVarBg::RelVarBg(const string& name)
    : name_(name)
{
    CheckIsIdentifier(name_);
}


const RichHeader& RelVarBg::GetRichHeader() const
{
    return access_ptr->GetRelVarRichHeader(name_);
}


const Constrs& RelVarBg::GetConstrs() const
{
    return access_ptr->GetRelVarConstrs(name_);
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, RelVarBg, GetNameCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/) const
{
    return String::New(name_.c_str());
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelVarBg, GetHeaderCb,
                    const Arguments&, /*args*/) const
{
    Handle<Object> result(Object::New());
    BOOST_FOREACH(const RichAttr& rich_attr, GetRichHeader())
        Set(result, rich_attr.GetName(),
            String::New(
                rich_attr.GetType().GetKuStr(rich_attr.GetTrait()).c_str()));
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelVarBg, CreateCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    if (!args[0]->IsObject())
        throw Error(Error::TYPE, "RelVar declaration must be an object");
    RichHeader rich_header;
    Constrs constrs;
    Handle<Object> object(args[0]->ToObject());
    PropEnumerator prop_enumerator(object);
    for (size_t i = 0; i < prop_enumerator.GetSize(); ++i) {
        Prop prop(prop_enumerator.GetProp(i));
        string name(Stringify(prop.key));
        CheckIsIdentifier(name);
        const TypeBg& type_bg(GetBg<TypeBg>(prop.value));
        rich_header.add_sure(RichAttr(name,
                                      type_bg.GetType(),
                                      type_bg.GetTrait(),
                                      type_bg.GetDefaultPtr()));
        type_bg.CollectConstrs(name, constrs);
    }
    constrs.reserve(constrs.size() + args.Length() - 1);
    for (int i = 1; i < args.Length(); ++i)
        constrs.push_back(GetBg<ConstrBg>(args[i]).GetConstr());
    access_ptr->CreateRelVar(name_, rich_header, constrs);
    return args.This();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelVarBg, InsertCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    if (!args[0]->IsObject())
        throw Error(Error::TYPE, "Insert argument must be an object");
    Handle<Object> object(args[0]->ToObject());
    PropEnumerator prop_enumerator(object);
    ValueMap value_map;
    for (size_t i = 0; i < prop_enumerator.GetSize(); ++i) {
        Prop prop(prop_enumerator.GetProp(i));
        string name(Stringify(prop.key));
        CheckIsIdentifier(name);
        value_map.insert(ValueMap::value_type(name, ReadKuValue(prop.value)));
    }
    const RichHeader& rich_header(GetRichHeader());
    Values values(access_ptr->Insert(name_, value_map));
    KU_ASSERT(values.size() == rich_header.size());
    Handle<Object> result(Object::New());
    for (size_t i = 0; i < values.size(); ++i)
        Set(result, rich_header[i].GetName(), MakeV8Value(values[i]));
    return result;
}    


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelVarBg, DropCb,
                    const Arguments&, /*args*/) const
{
    access_ptr->DropRelVar(name_);
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelVarBg, AllCb,
                    const Arguments&, /*args*/) const
{
    return JSNew<SelectionBg>(name_, Specs());
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelVarBg, GetIntegersCb,
                    const Arguments&, /*args*/) const
{
    Handle<Array> result(Array::New());
    int32_t i = 0;
    BOOST_FOREACH(const RichAttr& rich_attr, GetRichHeader())
        if (rich_attr.GetTrait() == Type::INTEGER ||
            rich_attr.GetTrait() == Type::SERIAL)
            result->Set(Integer::New(i++),
                        String::New(rich_attr.GetName().c_str()));
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelVarBg, GetSerialsCb,
                    const Arguments&, /*args*/) const
{
    Handle<Array> result(Array::New());
    int32_t i = 0;
    BOOST_FOREACH(const RichAttr& rich_attr, GetRichHeader())
        if (rich_attr.GetTrait() == Type::SERIAL)
            result->Set(Integer::New(i++),
                        String::New(rich_attr.GetName().c_str()));
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelVarBg, GetDefaultsCb,
                    const Arguments&, /*args*/) const
{
    Handle<Object> result(Object::New());
    BOOST_FOREACH(const RichAttr& rich_attr, GetRichHeader())
        if (rich_attr.GetDefaultPtr())
            Set(result, rich_attr.GetName(),
                MakeV8Value(*rich_attr.GetDefaultPtr()));
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelVarBg, GetUniquesCb,
                    const Arguments&, /*args*/) const
{
    Handle<Array> result(Array::New());
    int32_t i = 0;
    BOOST_FOREACH(const Constr& constr, GetConstrs()) {
        const Unique* unique_ptr = boost::get<Unique>(&constr);
        if (unique_ptr)
            result->Set(Integer::New(i++),
                        MakeV8Array(unique_ptr->field_names));
    }
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelVarBg, GetForeignsCb,
                    const Arguments&, /*args*/) const
{
    Handle<Array> result(Array::New());
    int32_t i = 0;
    BOOST_FOREACH(const Constr& constr, GetConstrs()) {
        const ForeignKey* foreign_key_ptr = boost::get<ForeignKey>(&constr);
        if (foreign_key_ptr) {
            Handle<Object> object(Object::New());
            Set(object, "keyFields",
                MakeV8Array(foreign_key_ptr->key_field_names));
            Set(object, "refRelVar",
                String::New(foreign_key_ptr->ref_rel_var_name.c_str()));
            Set(object, "refFields",
                MakeV8Array(foreign_key_ptr->ref_field_names));
            result->Set(Integer::New(i++), object);
        }
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////
// DBMediatorBg definitions
////////////////////////////////////////////////////////////////////////////////

DEFINE_JS_CLASS(DBMediatorBg, "_DBMediator",
                /*object_template*/, proto_template)
{
    RelBg::GetJSClass();
    TypeBg::GetJSClass();
    ConstrBg::GetJSClass();
    SetFunction(proto_template, "_query", QueryCb);
    SetFunction(proto_template, "_dropRelVars", DropRelVarsCb);
    SetFunction(proto_template, "_unique", UniqueCb);
    SetFunction(proto_template, "_foreign", ForeignCb);
    SetFunction(proto_template, "_check", CheckCb);
    SetFunction(proto_template, "_describeApp", DescribeAppCb);
    SetFunction(proto_template, "_getAdminedApps", GetAdminedAppsCb);
    SetFunction(proto_template, "_getDevelopedApps", GetDevelopedAppsCb);
    SetFunction(proto_template, "_getAppsByLabel", GetAppsByLabelCb);
}


DBMediatorBg::DBMediatorBg()
{
}


void DBMediatorBg::Init(v8::Handle<v8::Object> object) const
{
    Set(object, "number", JSNew<TypeBg>(Type::NUMBER), DontEnum);
    Set(object, "string", JSNew<TypeBg>(Type::STRING), DontEnum);
    Set(object, "bool", JSNew<TypeBg>(Type::BOOLEAN), DontEnum);
    Set(object, "date", JSNew<TypeBg>(Type::DATE), DontEnum);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBMediatorBg, QueryCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    string query_str(Stringify(args[0]));
    Values params;
    params.reserve(args.Length() - 1);
    for (int i = 1; i < args.Length(); ++i)
        params.push_back(ReadKuValue(args[i]));
    return JSNew<RelBg>(query_str, params, Specs());
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBMediatorBg, DropRelVarsCb,
                    const Arguments&, args) const
{
    StringSet rel_var_names(ReadStringSet(args));
    access_ptr->DropRelVars(rel_var_names);
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBMediatorBg, UniqueCb,
                    const Arguments&, args) const
{
    StringSet field_names(ReadStringSet(args));
    return JSNew<ConstrBg>(Unique(field_names));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBMediatorBg, ForeignCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 3);
    StringSet key_field_names(ReadStringSet(args[0]));
    StringSet ref_field_names(ReadStringSet(args[2]));
    return JSNew<ConstrBg>(ForeignKey(key_field_names,
                                      Stringify(args[1]),
                                      ref_field_names));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBMediatorBg, CheckCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    return JSNew<ConstrBg>(Check(Stringify(args[0])));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBMediatorBg, DescribeAppCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    App app(access_ptr->DescribeApp(Stringify(args[0])));
    Handle<Object> result(Object::New());
    Set(result, "admin", String::New(app.admin.c_str()));
    Set(result, "developers", MakeV8Array(app.developers));
    Set(result, "email", String::New(app.email.c_str()));
    Set(result, "summary", String::New(app.summary.c_str()));
    Set(result, "description", String::New(app.description.c_str()));
    Set(result, "labels", MakeV8Array(app.labels));
    return result;
}


#define DEFINE_JS_CALLBACK_APPS(name)                               \
    DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBMediatorBg, name##Cb,  \
                        const Arguments&, args) const {             \
        CheckArgsLength(args, 1);                                   \
        return MakeV8Array(access_ptr->name(Stringify(args[0])));   \
    }

DEFINE_JS_CALLBACK_APPS(GetAdminedApps)
DEFINE_JS_CALLBACK_APPS(GetDevelopedApps)
DEFINE_JS_CALLBACK_APPS(GetAppsByLabel)

////////////////////////////////////////////////////////////////////////////////
// DBBg definitions
////////////////////////////////////////////////////////////////////////////////

DEFINE_JS_CLASS(DBBg, "DB", object_template, /*proto_template*/)
{
    RelVarBg::GetJSClass();
    object_template->SetNamedPropertyHandler(GetRelVarCb,
                                             SetRelVarCb,
                                             HasRelVarCb,
                                             0,
                                             EnumRelVarsCb);
}


DBBg::DBBg()
{
}


DBBg::~DBBg()
{
    rel_vars_.Dispose();
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, DBBg, GetRelVarCb,
                    Local<String>, property,
                    const AccessorInfo&, /*info*/)
{
    if (rel_vars_.IsEmpty())
        rel_vars_ = Persistent<Object>::New(Object::New());
    Handle<v8::Value> result(rel_vars_->Get(property));
    if (!result->IsUndefined())
        return result;
    result = JSNew<RelVarBg>(Stringify(property));
    rel_vars_->Set(property, result);
    return result;
}


Handle<v8::Value> DBBg::SetRelVarCb(Local<String> /*property*/,
                                    Local<v8::Value> value,
                                    const AccessorInfo& /*info*/)
{
    return value;
}


DEFINE_JS_CALLBACK2(Handle<Boolean>, DBBg, HasRelVarCb,
                    Local<String>, property,
                    const AccessorInfo&, /*info*/) const
{
    return Boolean::New(access_ptr->HasRelVar(Stringify(property)));
}


DEFINE_JS_CALLBACK1(Handle<Array>, DBBg, EnumRelVarsCb,
                    const AccessorInfo&, /*info*/) const
{
    StringSet rel_var_name_set(access_ptr->GetRelVarNames());
    Handle<Array> result(Array::New(rel_var_name_set.size()));
    for (size_t i = 0; i < rel_var_name_set.size(); ++i)
        result->Set(Integer::New(i), String::New(rel_var_name_set[i].c_str()));
    return result;
}
