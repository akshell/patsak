
// (c) 2009-2010 by Anton Korenyushkin

#include "js-db.h"
#include "js-common.h"
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

namespace
{
    Persistent<Function> stringify_json_func;
    Persistent<Function> parse_json_func;
}

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
    if (type.IsNumeric())
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
// Utils
////////////////////////////////////////////////////////////////////////////////

namespace
{
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


    Handle<v8::Value> MakeV8Value(ak::Value ak_value)
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
        const ak::Value* GetDefaultPtr() const;
        void RetrieveConstrs(const string& attr_name,
                             UniqueKeySet& unique_key_set,
                             ForeignKeySet& foreign_key_set,
                             Strings& checks) const;

    private:
        typedef shared_ptr<AddedConstr> AddedConstrPtr;
        typedef vector<AddedConstrPtr> AddedConstrPtrs;

        Type type_;
        shared_ptr<ak::Value> def_ptr_;
        AddedConstrPtrs ac_ptrs_;

        friend Handle<Object> JSNew<TypeBg>(Type,
                                            shared_ptr<ak::Value>,
                                            AddedConstrPtrs);

        TypeBg(Type type,
               shared_ptr<ak::Value> def_ptr,
               const AddedConstrPtrs& ac_ptrs);

        DECLARE_JS_CALLBACK2(Handle<v8::Value>, GetNameCb,
                             Local<String>, const AccessorInfo&) const;

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
    SetFunction(proto_template, "_default", DefaultCb);
    SetFunction(proto_template, "_unique", UniqueCb);
    SetFunction(proto_template, "_foreign", ForeignCb);
    SetFunction(proto_template, "_check", CheckCb);
}


TypeBg::TypeBg(Type type)
    : type_(type)
{
}


TypeBg::TypeBg(Type type,
               shared_ptr<ak::Value> def_ptr,
               const AddedConstrPtrs& ac_ptrs)
    : type_(type)
    , def_ptr_(def_ptr)
    , ac_ptrs_(ac_ptrs)
{
}


Type TypeBg::GetType() const
{
    return type_;
}


const ak::Value* TypeBg::GetDefaultPtr() const
{
    return def_ptr_.get();
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


DEFINE_JS_CALLBACK1(Handle<v8::Value>, TypeBg, DefaultCb,
                    const Arguments&, args) const
{
    CheckArgsLength(args, 1);
    if (type_ == Type::SERIAL)
        throw Error(Error::USAGE, "Default and serial are incompatible");
    shared_ptr<ak::Value> def_ptr(
        new ak::Value(CreateDraft(args[0]).Get(type_)));
    return JSNew<TypeBg>(type_, def_ptr, ac_ptrs_);
}


Handle<v8::Value> TypeBg::NewWithAddedConstr(AddedConstrPtr ac_ptr) const
{
    AK_ASSERT(ac_ptr.get());
    AddedConstrPtrs new_ac_ptrs(ac_ptrs_);
    new_ac_ptrs.push_back(ac_ptr);
    return JSNew<TypeBg>(type_, def_ptr_, new_ac_ptrs);

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
// InitDB and RolledBack
////////////////////////////////////////////////////////////////////////////////

namespace
{
    bool rolled_back = false;


    DEFINE_JS_CALLBACK(RollBackCb, /*args*/)
    {
        rolled_back = true;
        return Undefined();
    }


    DEFINE_JS_CALLBACK(QueryCb, args)
    {
        CheckArgsLength(args, 6);
        Handle<Array> by_values(GetArray(args[2]));
        Strings by_strs;
        by_strs.reserve(by_values->Length());
        for (size_t i = 0; i < by_values->Length(); ++i)
            by_strs.push_back(Stringify(by_values->Get(Integer::New(i))));
        Header header;
        vector<Values> tuples;
        Query(header,
              tuples,
              Stringify(args[0]),
              ReadParams(args[1]),
              by_strs,
              ReadParams(args[3]),
              args[4]->Uint32Value(),
              (args[5]->IsUndefined() || args[5]->IsNull()
               ? MINUS_ONE
               : args[5]->Uint32Value()));
        Handle<Array> result(Array::New(tuples.size()));
        for (size_t tuple_idx = 0; tuple_idx < tuples.size(); ++tuple_idx) {
            Handle<Object> item(Object::New());
            const Values& values(tuples[tuple_idx]);
            AK_ASSERT_EQUAL(values.size(), header.size());
            for (size_t attr_idx = 0; attr_idx < header.size(); ++attr_idx)
                Set(item,
                    header[attr_idx].name,
                    MakeV8Value(values[attr_idx]));
            result->Set(Integer::New(tuple_idx), item);
        }
        return result;
    }


    DEFINE_JS_CALLBACK(CountCb, args)
    {
        CheckArgsLength(args, 2);
        return Integer::New(Count(Stringify(args[0]), ReadParams(args[1])));
    }


    DEFINE_JS_CALLBACK(CreateCb, args)
    {
        CheckArgsLength(args, 5);
        if (!args[1]->IsObject())
            throw Error(Error::TYPE, "Header must be an object");
        DefHeader def_header;
        UniqueKeySet unique_key_set;
        ForeignKeySet foreign_key_set;
        Strings checks;
        PropEnumerator prop_enumerator(args[1]->ToObject());
        for (size_t i = 0; i < prop_enumerator.GetSize(); ++i) {
            Prop prop(prop_enumerator.GetProp(i));
            string name(Stringify(prop.key));
            const TypeBg& type_bg(GetBg<TypeBg>(prop.value));
            ValuePtr value_ptr;
            if (type_bg.GetDefaultPtr())
                value_ptr = *type_bg.GetDefaultPtr();
            def_header.add_sure(DefAttr(name, type_bg.GetType(), value_ptr));
            type_bg.RetrieveConstrs(
                name, unique_key_set, foreign_key_set, checks);
        }
        ReadUniqueKeys(args[2], unique_key_set);
        ReadForeignKeySet(args[3], foreign_key_set);
        ReadChecks(args[4], checks);
        CreateRelVar(Stringify(args[0]),
                     def_header,
                     unique_key_set,
                     foreign_key_set,
                     checks);
        return Undefined();

    }


    DEFINE_JS_CALLBACK(DropCb, args)
    {
        CheckArgsLength(args, 1);
        DropRelVars(ReadStringSet(args[0]));
        return Undefined();
    }


    DEFINE_JS_CALLBACK(ListCb, /*args*/)
    {
        StringSet rel_var_name_set(GetRelVarNames());
        Handle<Array> result(Array::New(rel_var_name_set.size()));
        for (size_t i = 0; i < rel_var_name_set.size(); ++i)
            result->Set(Integer::New(i),
                        String::New(rel_var_name_set[i].c_str()));
        return result;
    }


    DEFINE_JS_CALLBACK(GetHeaderCb, args)
    {
        CheckArgsLength(args, 1);
        Handle<Object> result(Object::New());
        BOOST_FOREACH(const Attr& attr, GetHeader(Stringify(args[0])))
            Set(result, attr.name, String::New(attr.type.GetName().c_str()));
        return result;
    }


    DEFINE_JS_CALLBACK(GetDefaultCb, args)
    {
        CheckArgsLength(args, 1);
        Handle<Object> result(Object::New());
        BOOST_FOREACH(const DefAttr& def_attr, GetDefHeader(Stringify(args[0])))
            if (def_attr.def_ptr)
                Set(result, def_attr.name, MakeV8Value(*def_attr.def_ptr));
        return result;
    }


    DEFINE_JS_CALLBACK(GetUniqueCb, args)
    {
        CheckArgsLength(args, 1);
        const UniqueKeySet& unique_key_set(GetUniqueKeySet(Stringify(args[0])));
        Handle<Array> result(Array::New(unique_key_set.size()));
        for (size_t i = 0; i < unique_key_set.size(); ++i)
            result->Set(Integer::New(i), MakeV8Array(unique_key_set[i]));
        return result;
    }


    DEFINE_JS_CALLBACK(GetForeignCb, args)
    {
        CheckArgsLength(args, 1);
        const ForeignKeySet& foreign_key_set(
            GetForeignKeySet(Stringify(args[0])));
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


    DEFINE_JS_CALLBACK(InsertCb, args)
    {
        CheckArgsLength(args, 2);
        string name(Stringify(args[0]));
        Values values(Insert(name, ReadDraftMap(args[1])));
        const Header& header(GetHeader(name));
        AK_ASSERT_EQUAL(values.size(), header.size());
        Handle<Object> result(Object::New());
        for (size_t i = 0; i < values.size(); ++i)
            Set(result, header[i].name, MakeV8Value(values[i]));
        return result;
    }


    DEFINE_JS_CALLBACK(DelCb, args)
    {
        CheckArgsLength(args, 3);
        size_t rows_number = Delete(
            Stringify(args[0]), Stringify(args[1]), ReadParams(args[2]));
        return Number::New(static_cast<double>(rows_number));
    }


    DEFINE_JS_CALLBACK(UpdateCb, args)
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
        size_t rows_number = Update(
            Stringify(args[0]), Stringify(args[1]), ReadParams(args[2]),
            expr_map, ReadParams(args[4]));
        return Number::New(static_cast<double>(rows_number));
    }


    DEFINE_JS_CALLBACK(AddAttrsCb, args)
    {
        CheckArgsLength(args, 2);
        if (!args[1]->IsObject())
            throw Error(Error::TYPE,
                        "Attributes must be described by an object");
        PropEnumerator prop_enumerator(args[1]->ToObject());
        DefHeader def_attrs;
        def_attrs.reserve(prop_enumerator.GetSize());
        for (size_t i = 0; i < prop_enumerator.GetSize(); ++i) {
            Prop prop(prop_enumerator.GetProp(i));
            Handle<Array> descr(GetArray(prop.value));
            if (descr->Length() != 2)
                throw Error(
                    Error::VALUE,
                    "Each attribute must be described by a 2-item array");
            const TypeBg* type_ptr =
                TypeBg::GetJSClass().Cast(descr->Get(Integer::New(0)));
            if (!type_ptr)
                throw Error(Error::TYPE,
                            "First description item must be a db.Type object");
            if (type_ptr->GetType() == Type::SERIAL)
                throw Error(Error::NOT_IMPLEMENTED,
                            "Adding of serial attributes is not implemented");
            ak::Value value(
                CreateDraft(
                    descr->Get(Integer::New(1))).Get(type_ptr->GetType()));
            def_attrs.add_sure(
                DefAttr(Stringify(prop.key), type_ptr->GetType(), value));
        }
        AddAttrs(Stringify(args[0]), def_attrs);
        return Undefined();
    }


    DEFINE_JS_CALLBACK(DropAttrsCb, args)
    {
        CheckArgsLength(args, 2);
        DropAttrs(Stringify(args[0]), ReadStringSet(args[1]));
        return Undefined();
    }


    DEFINE_JS_CALLBACK(AddDefaultCb, args)
    {
        CheckArgsLength(args, 2);
        AddDefault(Stringify(args[0]), ReadDraftMap(args[1]));
        return Undefined();
    }


    DEFINE_JS_CALLBACK(DropDefaultCb, args)
    {
        CheckArgsLength(args, 2);
        DropDefault(Stringify(args[0]), ReadStringSet(args[1]));
        return Undefined();
    }


    DEFINE_JS_CALLBACK(AddConstrsCb, args)
    {
        CheckArgsLength(args, 4);
        UniqueKeySet unique_key_set;
        ForeignKeySet foreign_key_set;
        Strings checks;
        ReadUniqueKeys(args[1], unique_key_set);
        ReadForeignKeySet(args[2], foreign_key_set);
        ReadChecks(args[3], checks);
        AddConstrs(Stringify(args[0]), unique_key_set, foreign_key_set, checks);
        return Undefined();
    }


    DEFINE_JS_CALLBACK(DropAllConstrsCb, args)
    {
        CheckArgsLength(args, 1);
        DropAllConstrs(Stringify(args[0]));
        return Undefined();
    }
}


bool ak::RolledBack()
{
    bool result = rolled_back;
    rolled_back = false;
    return result;
}


Handle<Object> ak::InitDB()
{
    Handle<Object> json(
        Get(Context::GetCurrent()->Global(), "JSON")->ToObject());
    stringify_json_func = Persistent<Function>::New(
        Handle<Function>::Cast(Get(json, "stringify")));
    parse_json_func = Persistent<Function>::New(
        Handle<Function>::Cast(Get(json, "parse")));

    Handle<Object> result(Object::New());
    SetFunction(result, "rollback", RollBackCb);
    SetFunction(result, "query", QueryCb);
    SetFunction(result, "count", CountCb);
    SetFunction(result, "create", CreateCb);
    SetFunction(result, "drop", DropCb);
    SetFunction(result, "list", ListCb);
    SetFunction(result, "getHeader", GetHeaderCb);
    SetFunction(result, "getDefault", GetDefaultCb);
    SetFunction(result, "getUnique", GetUniqueCb);
    SetFunction(result, "getForeign", GetForeignCb);
    SetFunction(result, "insert", InsertCb);
    SetFunction(result, "del", DelCb);
    SetFunction(result, "update", UpdateCb);
    SetFunction(result, "addAttrs", AddAttrsCb);
    SetFunction(result, "dropAttrs", DropAttrsCb);
    SetFunction(result, "addDefault", AddDefaultCb);
    SetFunction(result, "dropDefault", DropDefaultCb);
    SetFunction(result, "addConstrs", AddConstrsCb);
    SetFunction(result, "dropAllConstrs", DropAllConstrsCb);
    Set(result, "number", JSNew<TypeBg>(Type::NUMBER));
    Set(result, "int", JSNew<TypeBg>(Type::INT));
    Set(result, "serial", JSNew<TypeBg>(Type::SERIAL));
    Set(result, "string", JSNew<TypeBg>(Type::STRING));
    Set(result, "bool", JSNew<TypeBg>(Type::BOOL));
    Set(result, "date", JSNew<TypeBg>(Type::DATE));
    Set(result, "json", JSNew<TypeBg>(Type::JSON));
    return result;
}
