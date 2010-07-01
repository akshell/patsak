
// (c) 2009-2010 by Anton Korenyushkin

#include "js-db.h"
#include "db.h"

#include <boost/foreach.hpp>
#include <boost/ref.hpp>


using namespace std;
using namespace ak;
using namespace v8;
using boost::ref;
using boost::shared_ptr;


////////////////////////////////////////////////////////////////////////////////
// JSON functions
////////////////////////////////////////////////////////////////////////////////

Persistent<Function> ak::stringify_json_func;
Persistent<Function> ak::parse_json_func;

////////////////////////////////////////////////////////////////////////////////
// Draft::Impl
////////////////////////////////////////////////////////////////////////////////

class Draft::Impl {
public:
    Impl(Handle<v8::Value> v8_value);
    ~Impl();
    ak::Value Get(Type type) const;

private:
    Persistent<v8::Value> v8_value_;
};


Draft::Impl::Impl(Handle<v8::Value> v8_value)
    : v8_value_(Persistent<v8::Value>::New(v8_value))
{
}


Draft::Impl::~Impl()
{
    v8_value_.Dispose();
}


ak::Value Draft::Impl::Get(Type type) const
{
    if (type == Type::NUMBER)
        return ak::Value(type, v8_value_->NumberValue());
    if (type == Type::STRING)
        return ak::Value(type, Stringify(v8_value_));
    if (type == Type::BOOL)
        return ak::Value(type, v8_value_->BooleanValue());
    if (type == Type::DATE) {
        if (!v8_value_->IsDate())
            throw Error(Error::TYPE, "Date expected");
        return ak::Value(type, v8_value_->NumberValue());
    }
    if (type == Type::JSON) {
        Handle<v8::Value> v8_value(v8_value_);
        Handle<v8::Value> json(
            stringify_json_func->Call(
                Context::GetCurrent()->Global(), 1, &v8_value));
        if (json.IsEmpty())
            throw Propagate();
        if (!json->IsString())
            throw Error(Error::TYPE, "Cannot serialize a value into JSON");
        return ak::Value(type, Stringify(json));
    }
    AK_ASSERT(type == Type::DUMMY);
    if (v8_value_->IsNumber())
        return ak::Value(Type::NUMBER, v8_value_->NumberValue());
    if (v8_value_->IsBoolean())
        return ak::Value(Type::BOOL, v8_value_->BooleanValue());
    if (v8_value_->IsDate())
        return ak::Value(Type::DATE, v8_value_->NumberValue());
    return ak::Value(Type::STRING, Stringify(v8_value_));
}

////////////////////////////////////////////////////////////////////////////////
// Draft definitions
////////////////////////////////////////////////////////////////////////////////

Draft::Draft(Impl* pimpl)
    : pimpl_(pimpl)
{
}


Draft::~Draft()
{
}


ak::Value Draft::Get(Type type) const
{
    return pimpl_->Get(type);
}

////////////////////////////////////////////////////////////////////////////////
// Utilities
////////////////////////////////////////////////////////////////////////////////

namespace
{
    const RichHeader& GetRichHeader(Handle<v8::Value> name)
    {
        return access_ptr->GetRichHeader(Stringify(name));
    }


    Draft CreateDraft(Handle<v8::Value> value)
    {
        return Draft(new Draft::Impl(value));
    }


    Drafts ReadParams(Handle<v8::Value> value)
    {
        Handle<Array> array(GetArray(value));
        Drafts result;
        result.reserve(array->Length());
        for (size_t i = 0; i < array->Length(); ++i)
            result.push_back(CreateDraft(array->Get(Integer::New(i))));
        return result;
    }


    Handle<v8::Value> MakeV8Value(const ak::Value& ak_value)
    {
        Type type(ak_value.GetType());
        double d;
        string s;
        ak_value.Get(d, s);
        if (type == Type::NUMBER)
            return Number::New(d);
        if (type == Type::STRING)
            return String::New(s.c_str());
        if (type == Type::BOOL)
            return Boolean::New(d);
        if (type == Type::DATE)
            return Date::New(d);
        AK_ASSERT(type == Type::JSON);
        Handle<v8::Value> arg(String::New(s.c_str()));
        Handle<v8::Value> result(
            parse_json_func->Call(Context::GetCurrent()->Global(), 1, &arg));
        if (result.IsEmpty())
            throw Propagate();
        return result;
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


    DraftMap ReadDraftMap(Handle<v8::Value> value)
    {
        if (!value->IsObject())
            throw Error(Error::TYPE, "Object required");
        PropEnumerator prop_enumerator(value->ToObject());
        DraftMap result;
        for (size_t i = 0; i < prop_enumerator.GetSize(); ++i) {
            Prop prop(prop_enumerator.GetProp(i));
            result.insert(DraftMap::value_type(Stringify(prop.key),
                                               CreateDraft(prop.value)));
        }
        return result;
    }


    void ReadUniqueKeys(Handle<v8::Value> value, UniqueKeySet& unique_key_set)
    {
        Handle<Array> array(GetArray(value));
        for (size_t i = 0; i < array->Length(); ++i)
            unique_key_set.add_unsure(
                ReadStringSet(array->Get(Integer::New(i))));
    }


    void ReadForeignKeySet(Handle<v8::Value> value,
                           ForeignKeySet& foreign_key_set)
    {
        Handle<Array> array(GetArray(value));
        for (size_t i = 0; i < array->Length(); ++i) {
            Handle<Array> foreign(GetArray(array->Get(Integer::New(i))));
            if (foreign->Length() != 3)
                throw Error(Error::VALUE,
                            "Foreign item must be an array of length 3");
            foreign_key_set.add_unsure(
                ForeignKey(ReadStringSet(foreign->Get(Integer::New(0))),
                           Stringify(foreign->Get(Integer::New(1))),
                           ReadStringSet(foreign->Get(Integer::New(2)))));
        }
    }


    void ReadChecks(Handle<v8::Value> value, Strings& checks)
    {
        Handle<Array> array(GetArray(value));
        for (size_t i = 0; i < array->Length(); ++i)
            checks.push_back(Stringify(array->Get(Integer::New(i))));
    }
}

////////////////////////////////////////////////////////////////////////////////
// AddedConstr
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class AddedConstr {
    public:
        virtual void Retrieve(const string& attr_name,
                              UniqueKeySet& unique_key_set,
                              ForeignKeySet& foreign_key_set,
                              Strings& checks) const = 0;

        virtual ~AddedConstr() {}
    };


    class AddedUnique : public AddedConstr {
    public:
        virtual void Retrieve(const string& attr_name,
                              UniqueKeySet& unique_key_set,
                              ForeignKeySet& /*foreign_key_set*/,
                              Strings& /*checks*/) const {
            StringSet unique_key;
            unique_key.add_sure(attr_name);
            unique_key_set.add_unsure(unique_key);
        }
    };


    class AddedForeignKey : public AddedConstr {
    public:
        AddedForeignKey(const string& ref_rel_var_name,
                        const string& ref_attr_name)
            : ref_rel_var_name_(ref_rel_var_name)
            , ref_attr_name_(ref_attr_name) {}

        virtual void Retrieve(const string& attr_name,
                              UniqueKeySet& /*unique_key_set*/,
                              ForeignKeySet& foreign_key_set,
                              Strings& /*checks*/) const {
            StringSet key_attr_names, ref_attr_names;
            key_attr_names.add_sure(attr_name);
            ref_attr_names.add_sure(ref_attr_name_);
            foreign_key_set.add_unsure(ForeignKey(key_attr_names,
                                                  ref_rel_var_name_,
                                                  ref_attr_names));
        }

    private:
        string ref_rel_var_name_;
        string ref_attr_name_;
    };


    class AddedCheck : public AddedConstr {
    public:
        AddedCheck(const string& expr_str)
            : check_(expr_str) {}

        virtual void Retrieve(const string& /*attr_name*/,
                              UniqueKeySet& /*unique_key_set*/,
                              ForeignKeySet& /*foreign_key_set*/,
                              Strings& checks) const {
            checks.push_back(check_);
        }

    private:
        string check_;
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
        const ak::Value* GetDefaultPtr() const;
        void RetrieveConstrs(const string& attr_name,
                             UniqueKeySet& unique_key_set,
                             ForeignKeySet& foreign_key_set,
                             Strings& checks) const;

    private:
        typedef shared_ptr<AddedConstr> AddedConstrPtr;
        typedef vector<AddedConstrPtr> AddedConstrPtrs;

        Type type_;
        Type::Trait trait_;
        shared_ptr<ak::Value> default_ptr_;
        AddedConstrPtrs ac_ptrs_;

        friend Handle<Object> JSNew<TypeBg>(Type,
                                            Type::Trait,
                                            shared_ptr<ak::Value>,
                                            AddedConstrPtrs);

        TypeBg(Type type,
               Type::Trait trait,
               shared_ptr<ak::Value> default_ptr,
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
               shared_ptr<ak::Value> default_ptr,
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


const ak::Value* TypeBg::GetDefaultPtr() const
{
    return default_ptr_.get();
}


void TypeBg::RetrieveConstrs(const string& attr_name,
                             UniqueKeySet& unique_key_set,
                             ForeignKeySet& foreign_key_set,
                             Strings& checks) const
{
    BOOST_FOREACH(const AddedConstrPtr& ac_ptr, ac_ptrs_) {
        AK_ASSERT(ac_ptr);
        ac_ptr->Retrieve(attr_name, unique_key_set, foreign_key_set, checks);
    }
}


DEFINE_JS_CALLBACK2(Handle<v8::Value>, TypeBg, GetNameCb,
                    Local<String>, /*property*/,
                    const AccessorInfo&, /*info*/) const
{
    return String::New(type_.GetName().c_str());
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
    shared_ptr<ak::Value> default_ptr(
        new ak::Value(CreateDraft(args[0]).Get(type_)));
    return JSNew<TypeBg>(type_, trait_, default_ptr, ac_ptrs_);
}


Handle<v8::Value> TypeBg::NewWithAddedConstr(AddedConstrPtr ac_ptr) const
{
    AK_ASSERT(ac_ptr.get());
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
    SetFunction(object_template, "addDefault", AddDefaultCb);
    SetFunction(object_template, "dropDefault", DropDefaultCb);
    SetFunction(object_template, "addConstrs", AddConstrsCb);
    SetFunction(object_template, "dropAllConstrs", DropAllConstrsCb);
}


DBBg::DBBg()
    : rolled_back_(false)
{
}


void DBBg::Init(v8::Handle<v8::Object> object) const
{
    Set(object, "number", JSNew<TypeBg>(Type::NUMBER));
    Set(object, "string", JSNew<TypeBg>(Type::STRING));
    Set(object, "bool", JSNew<TypeBg>(Type::BOOL));
    Set(object, "date", JSNew<TypeBg>(Type::DATE));
    Set(object, "json", JSNew<TypeBg>(Type::JSON));
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
        AK_ASSERT_EQUAL(values.size(), header.size());
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
    UniqueKeySet unique_key_set;
    ForeignKeySet foreign_key_set;
    Strings checks;
    PropEnumerator prop_enumerator(args[1]->ToObject());
    for (size_t i = 0; i < prop_enumerator.GetSize(); ++i) {
        Prop prop(prop_enumerator.GetProp(i));
        string name(Stringify(prop.key));
        const TypeBg& type_bg(GetBg<TypeBg>(prop.value));
        rich_header.add_sure(RichAttr(name,
                                      type_bg.GetType(),
                                      type_bg.GetTrait(),
                                      type_bg.GetDefaultPtr()));
        type_bg.RetrieveConstrs(name, unique_key_set, foreign_key_set, checks);
    }
    ReadUniqueKeys(args[2], unique_key_set);
    ReadForeignKeySet(args[3], foreign_key_set);
    ReadChecks(args[4], checks);
    access_ptr->Create(Stringify(args[0]),
                       rich_header,
                       unique_key_set,
                       foreign_key_set,
                       checks);
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
                rich_attr.GetType().GetName(rich_attr.GetTrait()).c_str()));
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
    const UniqueKeySet& unique_key_set(
        access_ptr->GetUniqueKeySet(Stringify(args[0])));
    Handle<Array> result(Array::New(unique_key_set.size()));
    for (size_t i = 0; i < unique_key_set.size(); ++i)
        result->Set(Integer::New(i), MakeV8Array(unique_key_set[i]));
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, GetForeignCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    const ForeignKeySet& foreign_key_set(
        access_ptr->GetForeignKeySet(Stringify(args[0])));
    Handle<Array> result(Array::New(foreign_key_set.size()));
    for (size_t i = 0; i < foreign_key_set.size(); ++i) {
        const ForeignKey& foreign_key(foreign_key_set[i]);
        Handle<Array> item(Array::New(3));
        item->Set(Integer::New(0), MakeV8Array(foreign_key.key_attr_names));
        item->Set(Integer::New(1),
                  String::New(foreign_key.ref_rel_var_name.c_str()));
        item->Set(Integer::New(2), MakeV8Array(foreign_key.ref_attr_names));
        result->Set(Integer::New(i), item);
    }
    return result;
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, InsertCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 2);
    string name(Stringify(args[0]));
    Values values(access_ptr->Insert(name, ReadDraftMap(args[1])));
    const RichHeader& rich_header(access_ptr->GetRichHeader(name));
    AK_ASSERT_EQUAL(values.size(), rich_header.size());
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
    StringMap expr_map;
    PropEnumerator prop_enumerator(args[3]->ToObject());
    for (size_t i = 0; i < prop_enumerator.GetSize(); ++i) {
        Prop prop(prop_enumerator.GetProp(i));
        expr_map.insert(StringMap::value_type(Stringify(prop.key),
                                              Stringify(prop.value)));
    }
    size_t rows_number = access_ptr->Update(
        Stringify(args[0]), Stringify(args[1]), ReadParams(args[2]),
        expr_map, ReadParams(args[4]));
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
        ak::Value value(
            CreateDraft(descr->Get(Integer::New(1))).Get(type_ptr->GetType()));
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


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, AddDefaultCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 2);
    access_ptr->AddDefault(Stringify(args[0]), ReadDraftMap(args[1]));
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, DropDefaultCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 2);
    access_ptr->DropDefault(Stringify(args[0]), ReadStringSet(args[1]));
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, AddConstrsCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 4);
    UniqueKeySet unique_key_set;
    ForeignKeySet foreign_key_set;
    Strings checks;
    ReadUniqueKeys(args[1], unique_key_set);
    ReadForeignKeySet(args[2], foreign_key_set);
    ReadChecks(args[3], checks);
    access_ptr->AddConstrs(Stringify(args[0]),
                           unique_key_set,
                           foreign_key_set,
                           checks);
    return Undefined();
}


DEFINE_JS_CALLBACK1(Handle<v8::Value>, DBBg, DropAllConstrsCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    access_ptr->DropAllConstrs(Stringify(args[0]));
    return Undefined();
}
