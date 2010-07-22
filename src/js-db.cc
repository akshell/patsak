// (c) 2009-2010 by Anton Korenyushkin

#include "js-db.h"
#include "js-common.h"
#include "db.h"


using namespace std;
using namespace ak;
using namespace v8;
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
        Drafts result;
        if (!value->BooleanValue())
            return result;
        Handle<Array> array(GetArray(value));
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
            if (!result.add_safely(Stringify(array->Get(Integer::New(i)))))
                throw Error(Error::VALUE, "Duplicating names");
        return result;
    }


    DraftMap ReadDraftMap(Handle<v8::Value> value)
    {
        if (!value->IsObject())
            throw Error(Error::TYPE, "Object required");
        PropEnumerator prop_enumerator(value->ToObject());
        DraftMap result;
        result.reserve(prop_enumerator.GetSize());
        for (size_t i = 0; i < prop_enumerator.GetSize(); ++i) {
            Prop prop(prop_enumerator.GetProp(i));
            result.add(
                NamedDraft(Stringify(prop.key), CreateDraft(prop.value)));
        }
        return result;
    }


    UniqueKeySet ReadUniqueKeys(Handle<v8::Value> value)
    {
        UniqueKeySet result;
        if (!value->BooleanValue())
            return result;
        Handle<Array> array(GetArray(value));
        result.reserve(array->Length());
        for (size_t i = 0; i < array->Length(); ++i)
            result.add_safely(ReadStringSet(array->Get(Integer::New(i))));
        return result;
    }


    ForeignKeySet ReadForeignKeySet(Handle<v8::Value> value)
    {
        ForeignKeySet result;
        if (!value->BooleanValue())
            return result;
        Handle<Array> array(GetArray(value));
        result.reserve(array->Length());
        for (size_t i = 0; i < array->Length(); ++i) {
            Handle<Array> foreign(GetArray(array->Get(Integer::New(i))));
            if (foreign->Length() != 3)
                throw Error(Error::VALUE,
                            "Foreign item must be an array of length 3");
            result.add_safely(
                ForeignKey(ReadStringSet(foreign->Get(Integer::New(0))),
                           Stringify(foreign->Get(Integer::New(1))),
                           ReadStringSet(foreign->Get(Integer::New(2)))));
        }
        return result;
    }


    Strings ReadChecks(Handle<v8::Value> value)
    {
        Strings result;
        if (!value->BooleanValue())
            return result;
        Handle<Array> array(GetArray(value));
        result.reserve(array->Length());
        for (size_t i = 0; i < array->Length(); ++i)
            result.push_back(Stringify(array->Get(Integer::New(i))));
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
            unique_key.add(attr_name);
            unique_key_set.add_safely(unique_key);
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
            key_attr_names.add(attr_name);
            ref_attr_names.add(ref_attr_name_);
            foreign_key_set.add_safely(
                ForeignKey(key_attr_names, ref_rel_var_name_, ref_attr_names));
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
// InitDB and RolledBack
////////////////////////////////////////////////////////////////////////////////

namespace
{
    bool rolled_back = false;


    DEFINE_JS_FUNCTION(RollBackCb, /*args*/)
    {
        rolled_back = true;
        return Undefined();
    }


    DEFINE_JS_FUNCTION(QueryCb, args)
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


    DEFINE_JS_FUNCTION(CountCb, args)
    {
        CheckArgsLength(args, 1);
        return Integer::New(Count(Stringify(args[0]), ReadParams(args[1])));
    }


    DEFINE_JS_FUNCTION(CreateCb, args)
    {
        CheckArgsLength(args, 2);
        if (!args[1]->IsObject())
            throw Error(Error::TYPE, "Header must be an object");
        DefHeader def_header;
        PropEnumerator prop_enumerator(args[1]->ToObject());
        for (size_t i = 0; i < prop_enumerator.GetSize(); ++i) {
            Prop prop(prop_enumerator.GetProp(i));
            Type type;
            ValuePtr default_ptr;
            if (prop.value->IsArray()) {
                Handle<Array> array(Handle<Array>::Cast(prop.value));
                if (array->Length() != 2)
                    throw Error(
                        Error::VALUE,
                        ("An attribute with a default value "
                         "must be described by a 2-item array"));
                type = ReadType(Stringify(array->Get(Integer::New(0))));
                default_ptr =
                    CreateDraft(array->Get(Integer::New(1))).Get(type);
            } else {
                type = ReadType(Stringify(prop.value));
            }
            def_header.add(DefAttr(Stringify(prop.key), type, default_ptr));
        }
        CreateRelVar(Stringify(args[0]),
                     def_header,
                     ReadUniqueKeys(args[2]),
                     ReadForeignKeySet(args[3]),
                     ReadChecks(args[4]));
        return Undefined();

    }


    DEFINE_JS_FUNCTION(DropCb, args)
    {
        CheckArgsLength(args, 1);
        DropRelVars(ReadStringSet(args[0]));
        return Undefined();
    }


    DEFINE_JS_FUNCTION(ListCb, /*args*/)
    {
        StringSet rel_var_name_set(GetRelVarNames());
        Handle<Array> result(Array::New(rel_var_name_set.size()));
        for (size_t i = 0; i < rel_var_name_set.size(); ++i)
            result->Set(Integer::New(i),
                        String::New(rel_var_name_set[i].c_str()));
        return result;
    }


    DEFINE_JS_FUNCTION(GetHeaderCb, args)
    {
        CheckArgsLength(args, 1);
        Handle<Object> result(Object::New());
        BOOST_FOREACH(const Attr& attr, GetHeader(Stringify(args[0])))
            Set(result, attr.name, String::New(attr.type.GetName().c_str()));
        return result;
    }


    DEFINE_JS_FUNCTION(GetDefaultCb, args)
    {
        CheckArgsLength(args, 1);
        Handle<Object> result(Object::New());
        BOOST_FOREACH(const DefAttr& def_attr, GetDefHeader(Stringify(args[0])))
            if (def_attr.default_ptr)
                Set(result, def_attr.name, MakeV8Value(*def_attr.default_ptr));
        return result;
    }


    DEFINE_JS_FUNCTION(GetUniqueCb, args)
    {
        CheckArgsLength(args, 1);
        const UniqueKeySet& unique_key_set(GetUniqueKeySet(Stringify(args[0])));
        Handle<Array> result(Array::New(unique_key_set.size()));
        for (size_t i = 0; i < unique_key_set.size(); ++i)
            result->Set(Integer::New(i), MakeV8Array(unique_key_set[i]));
        return result;
    }


    DEFINE_JS_FUNCTION(GetForeignCb, args)
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


    DEFINE_JS_FUNCTION(InsertCb, args)
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


    DEFINE_JS_FUNCTION(DelCb, args)
    {
        CheckArgsLength(args, 2);
        return Number::New(
            Delete(
                Stringify(args[0]), Stringify(args[1]), ReadParams(args[2])));
    }


    DEFINE_JS_FUNCTION(UpdateCb, args)
    {
        CheckArgsLength(args, 4);
        if (!args[3]->IsObject())
            throw Error(Error::TYPE, "Update needs an object");
        PropEnumerator prop_enumerator(args[3]->ToObject());
        StringMap expr_map;
        expr_map.reserve(prop_enumerator.GetSize());
        for (size_t i = 0; i < prop_enumerator.GetSize(); ++i) {
            Prop prop(prop_enumerator.GetProp(i));
            expr_map.add(
                NamedString(Stringify(prop.key), Stringify(prop.value)));
        }
        return Number::New(
            Update(
                Stringify(args[0]), Stringify(args[1]), ReadParams(args[2]),
                expr_map, ReadParams(args[4])));
    }


    DEFINE_JS_FUNCTION(AddAttrsCb, args)
    {
        CheckArgsLength(args, 2);
        if (!args[1]->IsObject())
            throw Error(Error::TYPE,
                        "Attributes must be described by an object");
        PropEnumerator prop_enumerator(args[1]->ToObject());
        ValHeader val_attr_set;
        val_attr_set.reserve(prop_enumerator.GetSize());
        for (size_t i = 0; i < prop_enumerator.GetSize(); ++i) {
            Prop prop(prop_enumerator.GetProp(i));
            Handle<Array> array(GetArray(prop.value));
            if (array->Length() != 2)
                throw Error(
                    Error::VALUE,
                    "Each attribute must be described by a 2-item array");
            Type type(ReadType(Stringify(array->Get(Integer::New(0)))));
            if (type == Type::SERIAL)
                throw Error(Error::NOT_IMPLEMENTED,
                            "Adding of serial attributes is not implemented");
            ak::Value value(
                CreateDraft(array->Get(Integer::New(1))).Get(type));
            val_attr_set.add(ValAttr(Stringify(prop.key), type, value));
        }
        AddAttrs(Stringify(args[0]), val_attr_set);
        return Undefined();
    }


    DEFINE_JS_FUNCTION(DropAttrsCb, args)
    {
        CheckArgsLength(args, 2);
        DropAttrs(Stringify(args[0]), ReadStringSet(args[1]));
        return Undefined();
    }


    DEFINE_JS_FUNCTION(AddDefaultCb, args)
    {
        CheckArgsLength(args, 2);
        AddDefault(Stringify(args[0]), ReadDraftMap(args[1]));
        return Undefined();
    }


    DEFINE_JS_FUNCTION(DropDefaultCb, args)
    {
        CheckArgsLength(args, 2);
        DropDefault(Stringify(args[0]), ReadStringSet(args[1]));
        return Undefined();
    }


    DEFINE_JS_FUNCTION(AddConstrsCb, args)
    {
        CheckArgsLength(args, 1);
        AddConstrs(Stringify(args[0]),
                   ReadUniqueKeys(args[1]),
                   ReadForeignKeySet(args[2]),
                   ReadChecks(args[3]));
        return Undefined();
    }


    DEFINE_JS_FUNCTION(DropAllConstrsCb, args)
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
    return result;
}
