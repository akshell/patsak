
// (c) 2009-2010 by Anton Korenyushkin

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
// GetRichHeader and GetConstrs
////////////////////////////////////////////////////////////////////////////////

namespace
{
    const RichHeader& GetRichHeader(Handle<v8::Value> name)
    {
        return access_ptr->GetRichHeader(Stringify(name));
    }
    

    const Constrs& GetConstrs(Handle<v8::Value> name)
    {
        return access_ptr->GetConstrs(Stringify(name));
    }
}

////////////////////////////////////////////////////////////////////////////////
// Readers
////////////////////////////////////////////////////////////////////////////////

namespace
{
    ku::Value ReadKuValue(Handle<v8::Value> v8_value)
    {
        if (v8_value->IsBoolean())
            return ku::Value(Type::BOOL, v8_value->BooleanValue());
        if (v8_value->IsNumber())
            return ku::Value(Type::NUMBER, v8_value->NumberValue());
        if (v8_value->IsDate())
            return ku::Value(Type::DATE, v8_value->NumberValue());
        return ku::Value(Type::STRING, Stringify(v8_value));
    }


    Values ReadParams(Handle<v8::Value> value)
    {
        Handle<Array> array(GetArray(value));
        Values result;
        result.reserve(array->Length());
        for (size_t i = 0; i < array->Length(); ++i)
            result.push_back(ReadKuValue(array->Get(Integer::New(i))));
        return result;
    }
    

    Handle<v8::Value> MakeV8Value(const ku::Value& ku_value)
    {
        Type type(ku_value.GetType());
        if (type == Type::NUMBER)
            return Number::New(ku_value.GetDouble());
        else if (type == Type::STRING)
            return String::New(ku_value.GetString().c_str());
        else if (type == Type::BOOL) 
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

    
    StringSet ReadStringSet(Handle<v8::Value> value)
    {
        Handle<Array> array(GetArray(value));
        StringSet result;
        result.reserve(array->Length());
        for (size_t i = 0; i < array->Length(); ++i)
            if (!result.add_unsure(Stringify(array->Get(Integer::New(i)))))
                throw Error(Error::VALUE, "Duplicating names");
        return result;
    }


    ValueMap ReadValueMap(Handle<v8::Value> value)
    {
        if (!value->IsObject())
            throw Error(Error::TYPE, "Object required");
        PropEnumerator prop_enumerator(value->ToObject());
        ValueMap result;
        for (size_t i = 0; i < prop_enumerator.GetSize(); ++i) {
            Prop prop(prop_enumerator.GetProp(i));
            result.insert(ValueMap::value_type(Stringify(prop.key),
                                               ReadKuValue(prop.value)));
        }
        return result;
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
// DBBg definitions
////////////////////////////////////////////////////////////////////////////////

DEFINE_JS_CLASS(DBBg, "DB", object_template, /*proto_template*/)
{
    TypeBg::GetJSClass();
    SetFunction(object_template, "rollback", RollBackCb);
    SetFunction(object_template, "query", QueryCb);
    SetFunction(object_template, "count", CountCb);
    SetFunction(object_template, "create", CreateCb);
    SetFunction(object_template, "drop", DropCb);
    SetFunction(object_template, "list", ListCb);
    SetFunction(object_template, "getHeader", GetHeaderCb);
    SetFunction(object_template, "getInteger", GetIntegerCb);
    SetFunction(object_template, "getSerial", GetSerialCb);
    SetFunction(object_template, "getDefault", GetDefaultCb);
    SetFunction(object_template, "getUnique", GetUniqueCb);
    SetFunction(object_template, "getForeign", GetForeignCb);
    SetFunction(object_template, "insert", InsertCb);
    SetFunction(object_template, "del", DelCb);
    SetFunction(object_template, "update", UpdateCb);
    SetFunction(object_template, "addAttrs", AddAttrsCb);
    SetFunction(object_template, "dropAttrs", DropAttrsCb);
    SetFunction(object_template, "setDefault", SetDefaultCb);
    SetFunction(object_template, "getAppDescription", GetAppDescriptionCb);
    SetFunction(object_template, "getAdminedApps", GetAdminedAppsCb);
    SetFunction(object_template, "getDevelopedApps", GetDevelopedAppsCb);
    SetFunction(object_template, "getAppsByLabel", GetAppsByLabelCb);
    SetFunction(object_template, "getUserEmail", GetUserEmailCb);
}


DBBg::DBBg(bool priviliged)
    : priviliged_(priviliged)
    , rolled_back_(false)
{
}


void DBBg::Init(v8::Handle<v8::Object> object) const
{
    Set(object, "number", JSNew<TypeBg>(Type::NUMBER));
    Set(object, "string", JSNew<TypeBg>(Type::STRING));
    Set(object, "bool", JSNew<TypeBg>(Type::BOOL));
    Set(object, "date", JSNew<TypeBg>(Type::DATE));
}


bool DBBg::WasRolledBack()
{
    bool result = rolled_back_;
    rolled_back_ = false;
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, RollBackCb,
                    const Arguments&, /*args*/)
{
    rolled_back_ = true;
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, QueryCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 6);
    Handle<Array> by_values(GetArray(args[2]));
    Strings by_strs;
    by_strs.reserve(by_values->Length());
    for (size_t i = 0; i < by_values->Length(); ++i)
        by_strs.push_back(Stringify(by_values->Get(Integer::New(i))));
    const QueryResult& query_result(
        access_ptr->Query(Stringify(args[0]),
                          ReadParams(args[1]),
                          by_strs,
                          ReadParams(args[3]),
                          args[4]->Uint32Value(),
                          (args[5]->IsUndefined() || args[5]->IsNull()
                           ? MINUS_ONE
                           : args[5]->Uint32Value())));
    size_t result_length = query_result.tuples.size();
    const Header& header(query_result.header);
    Handle<Array> result(Array::New(result_length));
    for (size_t tuple_idx = 0; tuple_idx < result_length; ++tuple_idx) {
        Handle<Object> item(Object::New());
        const Values& values(query_result.tuples[tuple_idx]);
        KU_ASSERT_EQUAL(values.size(), header.size());
        for (size_t attr_idx = 0; attr_idx < header.size(); ++attr_idx)
            Set(item,
                header[attr_idx].GetName(),
                MakeV8Value(values[attr_idx]));
        result->Set(Integer::New(tuple_idx), item);
    }
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, CountCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 2);
    return Integer::New(
        access_ptr->Count(Stringify(args[0]), ReadParams(args[1])));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, CreateCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 5);
    if (!args[1]->IsObject())
        throw Error(Error::TYPE, "Header must be an object");
    RichHeader rich_header;
    Constrs constrs;
    PropEnumerator prop_enumerator(args[1]->ToObject());
    for (size_t i = 0; i < prop_enumerator.GetSize(); ++i) {
        Prop prop(prop_enumerator.GetProp(i));
        string name(Stringify(prop.key));
        const TypeBg& type_bg(GetBg<TypeBg>(prop.value));
        rich_header.add_sure(RichAttr(name,
                                      type_bg.GetType(),
                                      type_bg.GetTrait(),
                                      type_bg.GetDefaultPtr()));
        type_bg.CollectConstrs(name, constrs);
    }
    Handle<Array> uniques(GetArray(args[2]));
    for (size_t i = 0; i < uniques->Length(); ++i)
        constrs.push_back(Unique(ReadStringSet(uniques->Get(Integer::New(i)))));
    Handle<Array> foreigns(GetArray(args[3]));
    for (size_t i = 0; i < foreigns->Length(); ++i) {
        Handle<Array> foreign(GetArray(foreigns->Get(Integer::New(i))));
        if (foreign->Length() != 3)
            throw Error(Error::VALUE,
                        "Foreign item must be an array of length 3");
        constrs.push_back(
            ForeignKey(ReadStringSet(foreign->Get(Integer::New(0))),
                       Stringify(foreign->Get(Integer::New(1))),
                       ReadStringSet(foreign->Get(Integer::New(2)))));
    }
    Handle<Array> checks(GetArray(args[4]));
    for (size_t i = 0; i < checks->Length(); ++i)
        constrs.push_back(Check(Stringify(checks->Get(Integer::New(i)))));
    access_ptr->Create(Stringify(args[0]), rich_header, constrs);
    return Undefined();
    
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, DropCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    access_ptr->Drop(ReadStringSet(args[0]));
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, ListCb,
                    const Arguments&, /*args*/) const
{
    StringSet rel_var_name_set(access_ptr->GetNames());
    Handle<Array> result(Array::New(rel_var_name_set.size()));
    for (size_t i = 0; i < rel_var_name_set.size(); ++i)
        result->Set(Integer::New(i), String::New(rel_var_name_set[i].c_str()));
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, GetHeaderCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    Handle<Object> result(Object::New());
    BOOST_FOREACH(const RichAttr& rich_attr, GetRichHeader(args[0]))
        Set(result, rich_attr.GetName(),
            String::New(
                rich_attr.GetType().GetKuStr(rich_attr.GetTrait()).c_str()));
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, GetIntegerCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    Handle<Array> result(Array::New());
    int32_t i = 0;
    BOOST_FOREACH(const RichAttr& rich_attr, GetRichHeader(args[0]))
        if (rich_attr.GetTrait() == Type::INTEGER ||
            rich_attr.GetTrait() == Type::SERIAL)
            result->Set(Integer::New(i++),
                        String::New(rich_attr.GetName().c_str()));
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, GetSerialCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    Handle<Array> result(Array::New());
    int32_t i = 0;
    BOOST_FOREACH(const RichAttr& rich_attr, GetRichHeader(args[0]))
        if (rich_attr.GetTrait() == Type::SERIAL)
            result->Set(Integer::New(i++),
                        String::New(rich_attr.GetName().c_str()));
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, GetDefaultCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    Handle<Object> result(Object::New());
    BOOST_FOREACH(const RichAttr& rich_attr, GetRichHeader(args[0]))
        if (rich_attr.GetDefaultPtr())
            Set(result, rich_attr.GetName(),
                MakeV8Value(*rich_attr.GetDefaultPtr()));
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, GetUniqueCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    Handle<Array> result(Array::New());
    int32_t i = 0;
    BOOST_FOREACH(const Constr& constr, GetConstrs(args[0])) {
        const Unique* unique_ptr = boost::get<Unique>(&constr);
        if (unique_ptr)
            result->Set(Integer::New(i++),
                        MakeV8Array(unique_ptr->field_names));
    }
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, GetForeignCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    Handle<Array> result(Array::New());
    int32_t i = 0;
    BOOST_FOREACH(const Constr& constr, GetConstrs(args[0])) {
        const ForeignKey* foreign_key_ptr = boost::get<ForeignKey>(&constr);
        if (foreign_key_ptr) {
            Handle<Array> item(Array::New(3));
            item->Set(Integer::New(0),
                      MakeV8Array(foreign_key_ptr->key_field_names));
            item->Set(Integer::New(1),
                      String::New(foreign_key_ptr->ref_rel_var_name.c_str()));
            item->Set(Integer::New(2),
                      MakeV8Array(foreign_key_ptr->ref_field_names));
            result->Set(Integer::New(i++), item);
        }
    }
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, InsertCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 2);
    string name(Stringify(args[0]));
    Values values(access_ptr->Insert(name, ReadValueMap(args[1])));
    const RichHeader& rich_header(access_ptr->GetRichHeader(name));
    KU_ASSERT_EQUAL(values.size(), rich_header.size());
    Handle<Object> result(Object::New());
    for (size_t i = 0; i < values.size(); ++i)
        Set(result, rich_header[i].GetName(), MakeV8Value(values[i]));
    return result;
}    


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, DelCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 3);
    size_t rows_number = access_ptr->Delete(
        Stringify(args[0]), Stringify(args[1]), ReadParams(args[2]));
    return Number::New(static_cast<double>(rows_number));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, UpdateCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 5);
    if (!args[3]->IsObject())
        throw Error(Error::TYPE, "Update needs an object");
    StringMap field_expr_map;
    PropEnumerator prop_enumerator(args[3]->ToObject());
    for (size_t i = 0; i < prop_enumerator.GetSize(); ++i) {
        Prop prop(prop_enumerator.GetProp(i));
        field_expr_map.insert(StringMap::value_type(Stringify(prop.key),
                                                    Stringify(prop.value)));
    }
    size_t rows_number = access_ptr->Update(
        Stringify(args[0]), Stringify(args[1]), ReadParams(args[2]),
        field_expr_map, ReadParams(args[4]));
    return Number::New(static_cast<double>(rows_number));
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, AddAttrsCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 2);
    if (!args[1]->IsObject())
        throw Error(Error::TYPE, "Attributes must be described by an object");
    PropEnumerator prop_enumerator(args[1]->ToObject());
    RichHeader rich_attrs;
    rich_attrs.reserve(prop_enumerator.GetSize());
    for (size_t i = 0; i < prop_enumerator.GetSize(); ++i) {
        Prop prop(prop_enumerator.GetProp(i));
        Handle<Array> descr(GetArray(prop.value));
        if (descr->Length() != 2)
            throw Error(Error::VALUE,
                        "Each attribute must be described by a 2-item array");
        const TypeBg* type_ptr =
            TypeBg::GetJSClass().Cast(descr->Get(Integer::New(0)));
        if (!type_ptr)
            throw Error(Error::TYPE,
                        "First description item must be a db.Type object");
        if (type_ptr->GetTrait() == Type::SERIAL)
            throw Error(Error::NOT_IMPLEMENTED,
                        "Adding of serial attributes is not implemented");
        const ku::Value& value(ReadKuValue(descr->Get(Integer::New(1))));
        rich_attrs.add_sure(RichAttr(Stringify(prop.key),
                                     type_ptr->GetType(),
                                     type_ptr->GetTrait(),
                                     &value));
    }
    access_ptr->AddAttrs(Stringify(args[0]), rich_attrs);
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, DropAttrsCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 2);
    access_ptr->DropAttrs(Stringify(args[0]), ReadStringSet(args[1]));
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, SetDefaultCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 2);
    access_ptr->SetDefault(Stringify(args[0]), ReadValueMap(args[1]));
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, GetAppDescriptionCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    App app(access_ptr->DescribeApp(Stringify(args[0])));
    Handle<Object> result(Object::New());
    Set(result, "admin", String::New(app.admin.c_str()));
    Set(result, "developers", MakeV8Array(app.developers));
    Set(result, "summary", String::New(app.summary.c_str()));
    Set(result, "description", String::New(app.description.c_str()));
    Set(result, "labels", MakeV8Array(app.labels));
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, GetUserEmailCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    if (!priviliged_)
        throw Error(Error::USAGE, "Application in not priviliged");
    return String::New(access_ptr->GetUserEmail(Stringify(args[0])).c_str());
}


#define DEFINE_JS_CALLBACK_APPS(name)                               \
    DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, name##Cb,  \
                        const Arguments&, args) const {             \
        CheckArgsLength(args, 1);                                   \
        return MakeV8Array(access_ptr->name(Stringify(args[0])));   \
    }

DEFINE_JS_CALLBACK_APPS(GetAdminedApps)
DEFINE_JS_CALLBACK_APPS(GetDevelopedApps)
DEFINE_JS_CALLBACK_APPS(GetAppsByLabel)
