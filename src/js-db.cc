
// (c) 2009 by Anton Korenyushkin

/// \file js-db.cc
/// JavaScript database stuff

#include "js-db.h"
#include "db.h"
#include "common.h"

#include <boost/foreach.hpp>
#include <boost/ref.hpp>
#include <boost/shared_ptr.hpp>


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
// Readers
////////////////////////////////////////////////////////////////////////////////

namespace
{
    bool ReadIdentifier(Handle<v8::Value> value, string& id)
    {
        id = Stringify(value);
        if (id.empty()) {
            JS_THROW(Error, "Identifier can't be empty");
            return false;
        }
        const locale& loc(locale::classic());
        if (id[0] != '_' && !isalpha(id[0], loc)) {
            JS_THROW(Error,
                     "First identifier character must be "
                     "a letter or underscore");
            return false;
        }
        for (size_t i = 1; i < id.size(); ++i) {
            if (id[i] != '_' && !isalnum(id[i], loc)) {
                JS_THROW(Error, "Identifier must consist only of "
                         "letters, digits or underscores");
                return false;
            }
        }
        return true;
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


    Handle<Array> MakeV8Array(const StringSet& string_set)
    {
        Handle<Array> result(Array::New());
        int32_t size = static_cast<int32_t>(string_set.size());
        for (int32_t i = 0; i < size; ++i)
            result->Set(Integer::New(i), String::New(string_set[i].c_str()));
        return result;
    }

    
    bool ReadStringSet(v8::Handle<v8::Value> value, StringSet& string_set)
    {
        string_set.clear();
        int32_t length = GetArrayLikeLength(value);
        if (length == -1)
            string_set.add_sure(Stringify(value));
        else {
            string_set.reserve(length);
            for (int32_t i = 0; i < length; ++i) {
                Handle<v8::Value> item(GetArrayLikeItem(value, i));
                if (item.IsEmpty())
                    return false;
                if (!string_set.add_unsure(Stringify(item))) {
                    JS_THROW(Error, "Duplicating names");
                    return false;
                }
            }
        }
        return true;
    }


    bool ReadStringSet(const Arguments& args, StringSet& string_set)
    {
        if (args.Length() == 1)
            return ReadStringSet(args[0], string_set);
        string_set.clear();
        string_set.reserve(args.Length());
        for (int i = 0; i < args.Length(); ++i) {
            if (!string_set.add_unsure(Stringify(args[i]))) {
                JS_THROW(Error, "Dumplicating names");
                return false;
            }
        }
        return true;
    }
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
        AddedForeignKey(const string& ref_rel_name,
                        const string& ref_field_name)
            : ref_rel_name_(ref_rel_name)
            , ref_field_name_(ref_field_name) {}
        
        virtual Constr GetConstr(const string& field_name) const {
            StringSet key_field_names, ref_field_names;
            key_field_names.add_sure(field_name);
            ref_field_names.add_sure(ref_field_name_);
            return ForeignKey(key_field_names, ref_rel_name_, ref_field_names);
        }

    private:
        string ref_rel_name_;
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

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, IntCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, SerialCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, ByDefaultCb,
                             const Arguments&) const;

        Handle<v8::Value> NewWithAddedConstr(AddedConstrPtr ac_ptr) const;
        
        DECLARE_JS_CALLBACK1(Handle<v8::Value>, UniqueCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, ForeignKeyCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, CheckCb,
                             const Arguments&) const;
    };
}


DEFINE_JS_CLASS(TypeBg, "Type", object_template, proto_template)
{
    object_template->SetAccessor(String::NewSymbol("name"), GetNameCb);
    proto_template->Set("int", FunctionTemplate::New(IntCb));
    proto_template->Set("serial", FunctionTemplate::New(SerialCb));
    proto_template->Set("byDefault", FunctionTemplate::New(ByDefaultCb));
    proto_template->Set("unique", FunctionTemplate::New(UniqueCb));
    proto_template->Set("foreignKey", FunctionTemplate::New(ForeignKeyCb));
    proto_template->Set("check", FunctionTemplate::New(CheckCb));
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


DEFINE_JS_CALLBACK1(Handle<v8::Value>, TypeBg, IntCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 0);
    JS_CHECK(trait_ == Type::COMMON, "Trait redefinition");
    return JSNew<TypeBg>(type_, Type::INT, default_ptr_, ac_ptrs_);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, TypeBg, SerialCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 0);
    JS_CHECK(trait_ == Type::COMMON, "Trait redefinition");
    JS_CHECK(!default_ptr_,
             "byDefault() and serial() are incompatible");
    return JSNew<TypeBg>(type_, Type::SERIAL, default_ptr_, ac_ptrs_);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, TypeBg, ByDefaultCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 1);
    JS_CHECK(trait_ != Type::SERIAL,
             "byDefault() and serial() are incompatible");
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
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 0);
    return NewWithAddedConstr(AddedConstrPtr(new AddedUnique()));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, TypeBg, ForeignKeyCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 2);
    AddedConstrPtr ac_ptr(new AddedForeignKey(Stringify(args[0]),
                                              Stringify(args[1])));
    return NewWithAddedConstr(ac_ptr);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, TypeBg, CheckCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 1);
    AddedConstrPtr ac_ptr(new AddedCheck(Stringify(args[0])));
    return NewWithAddedConstr(ac_ptr);
}

////////////////////////////////////////////////////////////////////////////////
// TypeCatalogBg definitions
////////////////////////////////////////////////////////////////////////////////

DEFINE_JS_CLASS(TypeCatalogBg, "TypeCatalog",
                object_template, /*proto_template*/)
{
    TypeBg::GetJSClass();
    object_template->SetNamedPropertyHandler(GetTypeCb,
                                             0,
                                             HasTypeCb,
                                             0,
                                             EnumTypesCb);
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, TypeCatalogBg, GetTypeCb,
                    Local<String>, property,
                    const AccessorInfo&, /*info*/) const
{
    string name(Stringify(property));
    if (name == Type(Type::NUMBER).GetKuStr())
        return JSNew<TypeBg>(Type::NUMBER);
    if (name == Type(Type::STRING).GetKuStr())
        return JSNew<TypeBg>(Type::STRING);
    if (name == Type(Type::BOOLEAN).GetKuStr())
        return JSNew<TypeBg>(Type::BOOLEAN);
    if (name == Type(Type::DATE).GetKuStr())
        return JSNew<TypeBg>(Type::DATE);
    return Handle<v8::Value>();
}


DEFINE_JS_CALLBACK2(Handle<Boolean>, TypeCatalogBg, HasTypeCb,
                    Local<String>, property,
                    const AccessorInfo&, /*info*/) const
{
    string name(Stringify(property));
    if (name == "number" || name == "string" || name == "boolean")
        return Boolean::New(true);
    return Boolean::New(false);
}


DEFINE_JS_CALLBACK1(Handle<Array>, TypeCatalogBg, EnumTypesCb,
                    const AccessorInfo&, /*info*/) const
{
    Handle<Array> result(Array::New());
    result->Set(Number::New(0), String::NewSymbol("number"));
    result->Set(Number::New(1), String::NewSymbol("string"));
    result->Set(Number::New(2), String::NewSymbol("boolean"));
    return result;
}

////////////////////////////////////////////////////////////////////////////////
// TupleBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Background of a tuple of a query result
    class TupleBg {
    public:
        DECLARE_JS_CLASS(TupleBg);
        
        TupleBg(const QueryResult& query_result, const Values& tuple);

    private:
        QueryResult query_result_;
        Values tuple_;

        const ku::Value* FindField(const string& name) const;

        DECLARE_JS_CALLBACK2(Handle<v8::Value>, GetFieldCb,
                             Local<String>, const AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(Handle<Boolean>, HasFieldCb,
                             Local<String>, const AccessorInfo&) const;

        DECLARE_JS_CALLBACK1(Handle<Array>, EnumFieldsCb,
                             const AccessorInfo&) const;
    };
}


DEFINE_JS_CLASS(TupleBg, "Tuple", object_template, /*proto_template*/)
{
    object_template->SetNamedPropertyHandler(GetFieldCb,
                                             0,
                                             HasFieldCb,
                                             0,
                                             EnumFieldsCb);
}


TupleBg::TupleBg(const QueryResult& query_result, const Values& tuple)
    : query_result_(query_result), tuple_(tuple)
{
}


const ku::Value* TupleBg::FindField(const string& name) const
{
    const Header& header(query_result_.GetHeader());
    for (size_t i = 0; i < header.size(); ++i)
        if (header[i].GetName() == name)
            return &tuple_[i];
    return 0;
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, TupleBg, GetFieldCb,
                    Local<String>, property,
                    const AccessorInfo&, /*info*/) const
{
    const ku::Value* ku_value_ptr = FindField(Stringify(property));
    if (!ku_value_ptr)
        return Handle<v8::Value>();
    return MakeV8Value(*ku_value_ptr);
}


DEFINE_JS_CALLBACK2(Handle<Boolean>, TupleBg, HasFieldCb,
                    Local<String>, property,
                    const AccessorInfo&, /*info*/) const
{
    const ku::Value* ku_value_ptr = FindField(Stringify(property));
    return Boolean::New(ku_value_ptr);
}


DEFINE_JS_CALLBACK1(Handle<Array>, TupleBg, EnumFieldsCb,
                    const AccessorInfo&, /*info*/) const
{
    const Header& header(query_result_.GetHeader());
    Handle<Array> result(Array::New(header.size()));
    for (size_t i = 0; i < header.size(); ++i)
        result->Set(Number::New(i), String::New(header[i].GetName().c_str()));
    return result;
}

////////////////////////////////////////////////////////////////////////////////
// QueryBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Background of a query result
    class QueryBg {
    public:
        DECLARE_JS_CLASS(QueryBg);
        
        QueryBg(const AccessHolder& access_holder,
                const string& query_str,
                const Values& params,
                const Specifiers& specifiers);
        virtual ~QueryBg();

    protected:
        static void InitObjectTemplate(Handle<ObjectTemplate> object_template);
        const AccessHolder& GetAccessHolder() const;
        string GetQueryStr() const;
        const Specifiers& GetSpecifiers() const;
        
    private:
        const AccessHolder& access_holder_;
        string query_str_;
        Values params_;
        Specifiers specifiers_;
        auto_ptr<QueryResult> query_result_ptr_;

        bool InitQueryResult();

        DECLARE_JS_CALLBACK2(Handle<v8::Value>, GetLengthCb,
                             Local<String>, const AccessorInfo&);

        DECLARE_JS_CALLBACK2(Handle<v8::Value>, GetTupleCb,
                             uint32_t, const AccessorInfo&);

        DECLARE_JS_CALLBACK2(Handle<Boolean>, HasTupleCb,
                             uint32_t, const AccessorInfo&);

        DECLARE_JS_CALLBACK1(Handle<Array>, EnumTuplesCb,
                             const AccessorInfo&);

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, PerformCb,
                             const Arguments&);        

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, OnlyCb,
                             const Arguments&) const;        

        template <typename SpecT>
        Handle<v8::Value> GenericSpecify(const Arguments& args) const;

        virtual Handle<v8::Value>
        InstantiateWithSpecifiers(const Specifiers& new_specifiers) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, WhereCb,
                             const Arguments&) const;        

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, ByCb,
                             const Arguments&) const;        
    };
}


DEFINE_JS_CLASS(QueryBg, "Query", object_template, proto_template)
{
    TupleBg::GetJSClass();
    InitObjectTemplate(object_template);
    proto_template->Set(String::NewSymbol("perform"),
                        FunctionTemplate::New(PerformCb),
                        DontEnum);

    proto_template->Set(String::NewSymbol("only"),
                        FunctionTemplate::New(OnlyCb),
                        DontEnum);

    proto_template->Set(String::NewSymbol("where"),
                        FunctionTemplate::New(WhereCb),
                        DontEnum);

    proto_template->Set(String::NewSymbol("by"),
                        FunctionTemplate::New(ByCb),
                        DontEnum);
}


QueryBg::QueryBg(const AccessHolder& access_holder,
                 const string& query_str,
                 const Values& params,
                 const Specifiers& specifiers)
    : access_holder_(access_holder)
    , query_str_(query_str)
    , params_(params)
    , specifiers_(specifiers)
{
}

QueryBg::~QueryBg()
{
    if (query_result_ptr_.get())
        V8::AdjustAmountOfExternalAllocatedMemory(
            -MEMORY_MULTIPLIER * query_result_ptr_->GetMemoryUsage());
}


void QueryBg::InitObjectTemplate(Handle<ObjectTemplate> object_template)
{
    object_template->SetAccessor(String::NewSymbol("length"),
                                 &GetLengthCb,
                                 0,
                                 Handle<v8::Value>(),
                                 DEFAULT,
                                 DontEnum);
    
    object_template->SetIndexedPropertyHandler(GetTupleCb,
                                               0,
                                               HasTupleCb,
                                               0,
                                               EnumTuplesCb);
}


const AccessHolder& QueryBg::GetAccessHolder() const
{
    return access_holder_;
}


string QueryBg::GetQueryStr() const
{
    return query_str_;
}


const Specifiers& QueryBg::GetSpecifiers() const
{
    return specifiers_;
}


bool QueryBg::InitQueryResult()
{
    if (query_result_ptr_.get())
        return true;
    try {
        QueryResult query_result(access_holder_->Query(query_str_,
                                                       params_,
                                                       specifiers_));
        query_result_ptr_.reset(new QueryResult(query_result));
        V8::AdjustAmountOfExternalAllocatedMemory(
            MEMORY_MULTIPLIER * query_result.GetMemoryUsage());
        return true;
    } catch (Error& err) {
        JS_THROW(Error, err.what());
        return false;
    }
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, QueryBg, GetLengthCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/)
{
    JS_CAN_THROW(InitQueryResult());
    return Integer::New(query_result_ptr_->GetSize());
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, QueryBg, GetTupleCb,
                    uint32_t, index,
                    const AccessorInfo&, /*info*/)
{
    JS_CAN_THROW(InitQueryResult());
    if (index >= query_result_ptr_->GetSize())
        return Handle<v8::Value>();
    return JSNew<TupleBg>(*query_result_ptr_,
                          query_result_ptr_->GetValues(index));
}


DEFINE_JS_CALLBACK2(Handle<Boolean>, QueryBg, HasTupleCb,
                    uint32_t, index,
                    const AccessorInfo&, /*info*/)
{
    // TODO may it's possible to throw
    if (!query_result_ptr_.get())
        return Boolean::New(false);
    return Boolean::New(index < query_result_ptr_->GetSize());
}


DEFINE_JS_CALLBACK1(Handle<Array>, QueryBg, EnumTuplesCb,
                    const AccessorInfo&, /*info*/)
{
    // TODO may it's possible to throw
    if (!query_result_ptr_.get())
        return Array::New(0);
    size_t tuples_size = query_result_ptr_->GetSize();
    Handle<Array> result(Array::New(tuples_size));
    for (size_t i = 0; i < tuples_size; ++i)
        result->Set(Number::New(i), Number::New(i));
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, QueryBg, PerformCb,
                    const Arguments&, args)
{
    JS_CHECK_LENGTH(args, 0);
    JS_CAN_THROW(InitQueryResult());
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, QueryBg, OnlyCb,
                    const Arguments&, args) const
{
    StringSet field_names;
    JS_CAN_THROW(ReadStringSet(args, field_names));
    Specifiers new_specifiers(specifiers_);
    new_specifiers.push_back(OnlySpecifier(field_names));
    return JSNew<QueryBg>(ref(access_holder_),
                          query_str_,
                          params_,
                          new_specifiers);
}


template <typename SpecT>
Handle<v8::Value> QueryBg::GenericSpecify(const Arguments& args) const
{
    JS_CHECK(args.Length() >= 1, "at least one argument required");
    string expr_str = Stringify(args[0]);
    Values params;
    params.reserve(args.Length() - 1);
    for (int i = 1; i < args.Length(); ++i)
        params.push_back(ReadKuValue(args[i]));
    Specifiers new_specifiers(specifiers_);
    new_specifiers.push_back(SpecT(expr_str, params));
    return InstantiateWithSpecifiers(new_specifiers);
}


Handle<v8::Value>
QueryBg::InstantiateWithSpecifiers(const Specifiers& new_specifiers) const
{
    return JSNew<QueryBg>(ref(access_holder_),
                          query_str_,
                          params_,
                          new_specifiers);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, QueryBg, WhereCb,
                    const Arguments&, args) const
{
    return GenericSpecify<WhereSpecifier>(args);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, QueryBg, ByCb,
                    const Arguments&, args) const
{
    return GenericSpecify<BySpecifier>(args);
}

////////////////////////////////////////////////////////////////////////////////
// SubRelBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Background of a subrelation
    class SubRelBg : public QueryBg {
    public:
        DECLARE_JS_CLASS(SubRelBg);
        
        SubRelBg(const AccessHolder& access_holder,
                 const string& rel_name,
                 const Specifiers& specifiers);

    private:
        string GetRelName() const;
        
        virtual Handle<v8::Value>
        InstantiateWithSpecifiers(const Specifiers& new_specifiers) const;

        WhereSpecifiers GetWhereSpecifiers() const;

        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, UpdateCb,
                             const Arguments&) const;
        
        DECLARE_JS_CALLBACK1(v8::Handle<v8::Value>, DelCb,
                             const Arguments&) const;
    };
}


DEFINE_JS_CLASS(SubRelBg, "SubRel", object_template, proto_template)
{
    InitObjectTemplate(object_template);
    
    proto_template->Set(String::NewSymbol("update"),
                        FunctionTemplate::New(UpdateCb),
                        DontEnum);

    proto_template->Set(String::NewSymbol("del"),
                        FunctionTemplate::New(DelCb),
                        DontEnum);
}


SubRelBg::SubRelBg(const AccessHolder& access_holder,
                   const string& rel_name,
                   const Specifiers& specifiers)
    : QueryBg(access_holder, rel_name, Values(), specifiers)
{
}


string SubRelBg::GetRelName() const
{
    return GetQueryStr();
}


Handle<v8::Value>
SubRelBg::InstantiateWithSpecifiers(const Specifiers& new_specifiers) const
{
    return JSNew<SubRelBg>(ref(GetAccessHolder()),
                           GetRelName(),
                           new_specifiers);
}


WhereSpecifiers SubRelBg::GetWhereSpecifiers() const
{
    WhereSpecifiers result;
    BOOST_FOREACH(const Specifier& specifier, GetSpecifiers()) {
        const WhereSpecifier*
            where_specifier_ptr = boost::get<WhereSpecifier>(&specifier);
        if (where_specifier_ptr)
            result.push_back(*where_specifier_ptr);
    }
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, SubRelBg, UpdateCb,
                    const Arguments&, args) const
{
    JS_CHECK(args.Length(),
             "update() must have at least one arguments");
    JS_TYPE_CHECK(args[0]->IsObject(),
                  "First update() argument must be object");
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
    JS_PROPAGATE(GetAccessHolder()->Update(GetRelName(),
                                           field_expr_map,
                                           params,
                                           GetWhereSpecifiers()));
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, SubRelBg, DelCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 0);
    JS_PROPAGATE(GetAccessHolder()->Delete(GetRelName(), GetWhereSpecifiers()));
    return Undefined();
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
// ConstrCatalogBg
////////////////////////////////////////////////////////////////////////////////

DEFINE_JS_CLASS(ConstrCatalogBg, "ConstrCatalog",
                /*object_template*/, proto_template)
{
    ConstrBg::GetJSClass();
    proto_template->Set("unique", FunctionTemplate::New(UniqueCb));
    proto_template->Set("foreignKey", FunctionTemplate::New(ForeignKeyCb));
    proto_template->Set("check", FunctionTemplate::New(CheckCb));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, ConstrCatalogBg, UniqueCb,
                    const Arguments&, args) const
{
    StringSet field_names;
    JS_CAN_THROW(ReadStringSet(args, field_names));
    return JSNew<ConstrBg>(Unique(field_names));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, ConstrCatalogBg, ForeignKeyCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 3);
    StringSet key_field_names, ref_field_names;
    JS_CAN_THROW(ReadStringSet(args[0], key_field_names) &&
                 ReadStringSet(args[2], ref_field_names));
    return JSNew<ConstrBg>(ForeignKey(key_field_names,
                                      Stringify(args[1]),
                                      ref_field_names));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, ConstrCatalogBg, CheckCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 1);
    return JSNew<ConstrBg>(Check(Stringify(args[0])));
}

////////////////////////////////////////////////////////////////////////////////
// AccessHolder definitions
////////////////////////////////////////////////////////////////////////////////

AccessHolder::Scope::Scope(AccessHolder& access_holder, Access& access)
    : access_holder_(access_holder)
{
    KU_ASSERT(!access_holder_.access_ptr_);
    access_holder_.access_ptr_ = &access;
}


AccessHolder::Scope::~Scope()
{
    KU_ASSERT(access_holder_.access_ptr_);
    access_holder_.access_ptr_ = 0;
}


AccessHolder::AccessHolder()
    : access_ptr_(0)
{
}


Access& AccessHolder::operator*() const
{
    KU_ASSERT(access_ptr_);
    return *access_ptr_;
}


Access* AccessHolder::operator->() const
{
    KU_ASSERT(access_ptr_);
    return access_ptr_;
}

////////////////////////////////////////////////////////////////////////////////
// RelCreator
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Relation creator functor
    class RelCreator {
    public:
        RelCreator(Access& access);
        Handle<v8::Value> operator()(const Arguments& args) const;

    private:
        Access& access_;

        static bool ReadHeader(Handle<v8::Value> value,
                               RichHeader& rich_header,
                               Constrs& constrs);
        
        static bool ReadConstrs(const Arguments& args, Constrs& constrs);
        
    };
}


RelCreator::RelCreator(Access& access)
    : access_(access)
{
}


Handle<v8::Value> RelCreator::operator()(const Arguments& args) const
{
    JS_CHECK(args.Length() >= 2, "createRel() must have at least two arguments");
    string name;
    RichHeader rich_header;
    Constrs constrs;
    JS_CAN_THROW(ReadIdentifier(args[0], name) &&
                 ReadHeader(args[1], rich_header, constrs) &&
                 ReadConstrs(args, constrs));
    JS_PROPAGATE(access_.CreateRel(name, rich_header, constrs));
    return Undefined();
}


bool RelCreator::ReadHeader(Handle<v8::Value> value,
                            RichHeader& rich_header,
                            Constrs& constrs)
{
    if (!value->IsObject()) {
        JS_THROW(TypeError, "Second createRel() argument must be object");
        return false;
    }
    Handle<Object> object(value->ToObject());
    PropEnumerator prop_enumerator(object);
    for (size_t i = 0; i < prop_enumerator.GetSize(); ++i) {
        Prop prop(prop_enumerator.GetProp(i));
        string name;
        if (!ReadIdentifier(prop.key, name))
            return false;
        const TypeBg* type_bg_ptr = TypeBg::GetJSClass().Cast(prop.value);
        if (!type_bg_ptr) {
            JS_THROW(Error, "Non type expression: " + Stringify(prop.value));
            return false;
        }
        rich_header.add_sure(RichAttr(name,
                                      type_bg_ptr->GetType(),
                                      type_bg_ptr->GetTrait(),
                                      type_bg_ptr->GetDefaultPtr()));
        type_bg_ptr->CollectConstrs(name, constrs);
    }
    return true;
}


bool RelCreator::ReadConstrs(const Arguments& args, Constrs& constrs)
{
    constrs.reserve(constrs.size() + args.Length() - 2);
    for (int i = 2; i < args.Length(); ++i) {
        const ConstrBg* constr_bg_ptr = ConstrBg::GetJSClass().Cast(args[i]);
        if (!constr_bg_ptr) {
            JS_THROW(TypeError,
                     "createRel arguments starting from the "
                     "third must be constraints");
            return false;
        }
        constrs.push_back(constr_bg_ptr->GetConstr());
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// DBBg definitions
////////////////////////////////////////////////////////////////////////////////

DEFINE_JS_CLASS(DBBg, "DB", /*object_template*/, proto_template)
{
    QueryBg::GetJSClass();
    proto_template->Set("query", FunctionTemplate::New(QueryCb));
    proto_template->Set("createRel", FunctionTemplate::New(CreateRelCb));
    proto_template->Set("dropRels", FunctionTemplate::New(DropRelsCb));
}


DBBg::DBBg(const AccessHolder& access_holder)
    : access_holder_(access_holder)
{
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, QueryCb,
                    const Arguments&, args) const
{
    JS_CHECK(args.Length() >= 1, "query() must have at least one argument");
    string query_str(Stringify(args[0]));
    Values params;
    params.reserve(args.Length() - 1);
    for (int i = 1; i < args.Length(); ++i)
        params.push_back(ReadKuValue(args[i]));
    return JSNew<QueryBg>(ref(access_holder_), query_str, params, Specifiers());
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, CreateRelCb,
                    const Arguments&, args) const
{
    return RelCreator(*access_holder_)(args);
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, DropRelsCb,
                    const Arguments&, args) const
{
    StringSet rel_names;
    JS_CAN_THROW(ReadStringSet(args, rel_names));
    JS_PROPAGATE(access_holder_->DeleteRels(rel_names));
    return Undefined();
}

////////////////////////////////////////////////////////////////////////////////
// Inserter
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Tuple inserter functor
    class Inserter {
    public:
        Inserter(Access& access, const string& rel_name);
        Handle<v8::Value> operator()(const Arguments& args) const;

    private:
        Access& access_;
        string rel_name_;
    };
}


Inserter::Inserter(Access& access, const string& rel_name)
    : access_(access)
    , rel_name_(rel_name)
{
}


Handle<v8::Value> Inserter::operator()(const Arguments& args) const
{
    JS_CHECK_LENGTH(args, 1);
    JS_TYPE_CHECK(args[0]->IsObject(), "insert() argument must be object");
    Handle<Object> object(args[0]->ToObject());
    PropEnumerator prop_enumerator(object);
    ValueMap value_map;
    for (size_t i = 0; i < prop_enumerator.GetSize(); ++i) {
        Prop prop(prop_enumerator.GetProp(i));
        string name;
        JS_CAN_THROW(ReadIdentifier(prop.key, name));
        value_map.insert(ValueMap::value_type(name, ReadKuValue(prop.value)));
    }
    JS_PROPAGATE(access_.Insert(rel_name_, value_map));
    return Undefined();
}

////////////////////////////////////////////////////////////////////////////////
// RelBg
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// rel.* background
    class RelBg {
    public:
        DECLARE_JS_CLASS(RelBg);

        RelBg(const AccessHolder& access_holder, const string& name);
        
    private:
        const AccessHolder& access_holder_;
        string name_;

        DECLARE_JS_CALLBACK2(Handle<v8::Value>, GetNameCb,
                             Local<String>, const AccessorInfo&) const;

        DECLARE_JS_CALLBACK2(Handle<v8::Value>, GetHeaderCb,
                             Local<String>, const AccessorInfo&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, InsertCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, DropCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, AllCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, GetIntsCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, GetSerialsCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, GetDefaultsCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, GetUniquesCb,
                             const Arguments&) const;

        DECLARE_JS_CALLBACK1(Handle<v8::Value>, GetForeignKeysCb,
                             const Arguments&) const;
    };
}


DEFINE_JS_CLASS(RelBg, "Rel", object_template, proto_template)
{
    SubRelBg::GetJSClass();
    QueryBg::GetJSClass().AddSubClass(SubRelBg::GetJSClass());
    object_template->SetAccessor(String::NewSymbol("name"), GetNameCb);
    object_template->SetAccessor(String::NewSymbol("header"), GetHeaderCb);
    proto_template->Set("insert", FunctionTemplate::New(InsertCb));
    proto_template->Set("drop", FunctionTemplate::New(DropCb));
    proto_template->Set("all", FunctionTemplate::New(AllCb));
    proto_template->Set("getInts", FunctionTemplate::New(GetIntsCb));
    proto_template->Set("getSerials", FunctionTemplate::New(GetSerialsCb));
    proto_template->Set("getDefaults", FunctionTemplate::New(GetDefaultsCb));
    proto_template->Set("getUniques", FunctionTemplate::New(GetUniquesCb));
    proto_template->Set("getForeignKeys",
                        FunctionTemplate::New(GetForeignKeysCb));
}


RelBg::RelBg(const AccessHolder& access_holder,
                             const string& name)
    : access_holder_(access_holder)
    , name_(name)
{
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, RelBg, GetNameCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/) const
{
    return String::New(name_.c_str());
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, RelBg, GetHeaderCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/) const
{
    const RichHeader& rich_header(access_holder_->GetRelRichHeader(name_));
    Handle<Object> result(Object::New());
    BOOST_FOREACH(const RichAttr& rich_attr, rich_header)
        result->Set(String::New(rich_attr.GetName().c_str()),
                    JSNew<TypeBg>(rich_attr.GetType()));
    return result;
}

DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelBg, InsertCb,
                    const Arguments&, args) const
{
    return Inserter(*access_holder_, name_)(args);
}    


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelBg, DropCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 0);    
    JS_PROPAGATE(access_holder_->DeleteRel(name_));
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelBg, AllCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 0);    
    return JSNew<SubRelBg>(ref(access_holder_),
                           name_,
                           Specifiers());
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelBg, GetIntsCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 0);
    const RichHeader& rich_header(access_holder_->GetRelRichHeader(name_));
    Handle<Array> result(Array::New());
    int32_t i = 0;
    BOOST_FOREACH(const RichAttr& rich_attr, rich_header)
        if (rich_attr.GetTrait() == Type::INT ||
            rich_attr.GetTrait() == Type::SERIAL)
            result->Set(Integer::New(i++),
                        String::New(rich_attr.GetName().c_str()));
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelBg, GetSerialsCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 0);
    const RichHeader& rich_header(access_holder_->GetRelRichHeader(name_));
    Handle<Array> result(Array::New());
    int32_t i = 0;
    BOOST_FOREACH(const RichAttr& rich_attr, rich_header)
        if (rich_attr.GetTrait() == Type::SERIAL)
            result->Set(Integer::New(i++),
                        String::New(rich_attr.GetName().c_str()));
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelBg, GetDefaultsCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 0);
    const RichHeader& rich_header(access_holder_->GetRelRichHeader(name_));
    Handle<Object> result(Object::New());
    BOOST_FOREACH(const RichAttr& rich_attr, rich_header)
        if (rich_attr.GetDefaultPtr())
            result->Set(String::New(rich_attr.GetName().c_str()),
                        MakeV8Value(*rich_attr.GetDefaultPtr()));
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelBg, GetUniquesCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 0);
    Handle<Array> result(Array::New());
    int32_t i = 0;
    const Constrs& constrs(access_holder_->GetRelConstrs(name_));
    BOOST_FOREACH(const Constr& constr, constrs) {
        const Unique* unique_ptr = boost::get<Unique>(&constr);
        if (unique_ptr)
            result->Set(Integer::New(i++),
                        MakeV8Array(unique_ptr->field_names));
    }
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, RelBg, GetForeignKeysCb,
                    const Arguments&, args) const
{
    JS_CHECK_LENGTH(args, 0);
    Handle<Array> result(Array::New());
    int32_t i = 0;
    const Constrs& constrs(access_holder_->GetRelConstrs(name_));
    BOOST_FOREACH(const Constr& constr, constrs) {
        const ForeignKey* foreign_key_ptr = boost::get<ForeignKey>(&constr);
        if (foreign_key_ptr) {
            Handle<Object> object(Object::New());
            object->Set(String::NewSymbol("key_fields"),
                        MakeV8Array(foreign_key_ptr->key_field_names));
            object->Set(String::NewSymbol("ref_rel"),
                        String::New(foreign_key_ptr->ref_rel_name.c_str()));
            object->Set(String::NewSymbol("ref_fields"),
                        MakeV8Array(foreign_key_ptr->ref_field_names));
            result->Set(Integer::New(i++), object);
        }
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////
// RelCatalogBg definitions
////////////////////////////////////////////////////////////////////////////////

DEFINE_JS_CLASS(RelCatalogBg, "RelCatalog", object_template, /*proto_template*/)
{
    RelBg::GetJSClass();
    object_template->SetNamedPropertyHandler(GetRelCb,
                                             0,
                                             HasRelCb,
                                             0,
                                             EnumRelsCb);
}


RelCatalogBg::RelCatalogBg(const AccessHolder& access_holder)
    : access_holder_(access_holder)
{
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, RelCatalogBg, GetRelCb,
                    Local<String>, property,
                    const AccessorInfo&, /*info*/) const
{
    string rel_name(Stringify(property));
    if (!access_holder_->HasRel(rel_name))
        return Handle<v8::Value>();
    return JSNew<RelBg>(ref(access_holder_), rel_name);
}


DEFINE_JS_CALLBACK2(Handle<Boolean>, RelCatalogBg, HasRelCb,
                    Local<String>, property,
                    const AccessorInfo&, /*info*/) const
{
    string rel_name(Stringify(property));
    return Boolean::New(access_holder_->HasRel(rel_name));
}


DEFINE_JS_CALLBACK1(Handle<Array>, RelCatalogBg, EnumRelsCb,
                    const AccessorInfo&, /*info*/) const
{
    StringSet rel_name_set(access_holder_->GetRelNames());
    Handle<Array> result(Array::New());
    for (size_t i = 0; i < rel_name_set.size(); ++i)
        result->Set(Integer::New(i), String::New(rel_name_set[i].c_str()));
    return result;
}