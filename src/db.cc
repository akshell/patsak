
// (c) 2008-2010 by Anton Korenyushkin

#include "db.h"
#include "translator.h"

#include <pqxx/pqxx>
#include <boost/format.hpp>
#include <boost/foreach.hpp>


using namespace std;
using namespace ak;
using boost::format;


////////////////////////////////////////////////////////////////////////////////
// Constants
////////////////////////////////////////////////////////////////////////////////

namespace
{
    const size_t MAX_NAME_SIZE = 60;
    const size_t MAX_ATTR_NUMBER = 500;
    const size_t MAX_REL_VAR_NUMBER = 500;
}

////////////////////////////////////////////////////////////////////////////////
// BOOST_FOREACH extensions
///////////////////////////////////////////////////////////////////////////////

namespace boost
{
    // For BOOST_FOREACH to work with pqxx::result
    template<>
    struct range_mutable_iterator<pqxx::result>
    {
        typedef pqxx::result::const_iterator type;
    };


    // For BOOST_FOREACH to work with pqxx::result::tuple
    template<>
    struct range_mutable_iterator<pqxx::result::tuple>
    {
        typedef pqxx::result::tuple::const_iterator type;
    };
}

////////////////////////////////////////////////////////////////////////////////
// RichAttr definitions
///////////////////////////////////////////////////////////////////////////////

RichAttr::RichAttr(const string& name,
                   Type type,
                   const Value* default_ptr)
    : attr_(name, type)
{
    AK_ASSERT(!(type == Type::SERIAL && default_ptr));
    SetDefaultPtr(default_ptr);
}


const Attr& RichAttr::GetAttr() const
{
    return attr_;
}


const string& RichAttr::GetName() const
{
    return attr_.GetName();
}


Type RichAttr::GetType() const
{
    return attr_.GetType();
}


const Value* RichAttr::GetDefaultPtr() const
{
    return default_ptr_.get();
}


void RichAttr::SetDefaultPtr(const Value* default_ptr)
{
    default_ptr_.reset(default_ptr ? new Value(*default_ptr) : 0);
}

////////////////////////////////////////////////////////////////////////////////
// RelVar and Meta declarations
///////////////////////////////////////////////////////////////////////////////

namespace
{
    class Meta;


    class RelVar {
    public:
        RelVar(const string& name);

        RelVar(const Meta& meta,
               const string& name,
               const RichHeader& rich_header,
               const UniqueKeySet& unique_key_set,
               const ForeignKeySet& foreign_key_set,
               const Strings& checks);

        void LoadConstrs(const Meta& meta);
        string GetName() const;
        const RichHeader& GetRichHeader() const;
        const Header& GetHeader() const;
        const UniqueKeySet& GetUniqueKeySet() const;
        const ForeignKeySet& GetForeignKeySet() const;
        void AddAttrs(const RichHeader& attrs);
        void DropAttrs(const StringSet& attr_names);
        void AddDefault(const DraftMap& draft_map);
        void DropDefault(const StringSet& attr_names);

        void AddConstrs(const Meta& meta,
                        const UniqueKeySet& unique_key_set,
                        const ForeignKeySet& foreign_key_set,
                        const Strings& checks);

        void DropAllConstrs();

    private:
        string name_;
        RichHeader rich_header_;
        Header header_;
        UniqueKeySet unique_key_set_;
        ForeignKeySet foreign_key_set_;

        static bool Intersect(const StringSet& lhs, const StringSet& rhs);
        static void CheckName(const string& name);
        static void CheckAttrNumber(size_t number);
        static void PrintAttrNames(ostream& os, const StringSet& names);
        StringSet ReadAttrNames(const string& pg_array) const;

        void PrintUniqueKey(ostream& os, const StringSet& unique_key) const;

        void PrintForeignKey(ostream& os,
                             const Meta& meta,
                             const ForeignKey& foreign_key) const;

        void PrintCheck(ostream& os, const string& check) const;

        void InitHeader();
    };


    typedef vector<RelVar> RelVars;


    class Meta {
    public:
        Meta(const string& schema_name);
        const RelVar& Get(const string& rel_var_name) const;
        RelVar& Get(const string& rel_var_name);
        const RelVars& GetAll() const;

        void Create(const string& rel_var_name,
                    const RichHeader& rich_header,
                    const UniqueKeySet& unique_key_set,
                    const ForeignKeySet& foreign_key_set,
                    const Strings& checks);

        void Drop(const StringSet& rel_var_names);

    private:
        RelVars rel_vars_;

        size_t GetIdx(const string& rel_var_name) const; // never throws
        size_t GetIdxChecked(const string& rel_var_name) const;
    };


    pqxx::result Exec(const string& sql);
    pqxx::result ExecSafely(const string& sql);
}

////////////////////////////////////////////////////////////////////////////////
// RelVar definitions
////////////////////////////////////////////////////////////////////////////////

RelVar::RelVar(const string& name)
    : name_(name)
{
    static const format query("SELECT * FROM ak.describe_table('\"%1%\"');");
    pqxx::result pqxx_result = Exec((format(query) % name_).str());
    rich_header_.reserve(pqxx_result.size());
    BOOST_FOREACH(const pqxx::result::tuple& tuple, pqxx_result) {
        AK_ASSERT_EQUAL(tuple.size(), 3U);
        AK_ASSERT(!tuple[0].is_null() && ! tuple[1].is_null());
        string name(tuple[0].c_str());
        Type type(tuple[1].c_str());
        auto_ptr<Value> default_ptr;
        if (!tuple[2].is_null()) {
            string default_str(tuple[2].c_str());
            if (default_str.substr(0, 8) == "nextval(") {
                AK_ASSERT(type == Type::INT);
                type = Type::SERIAL;
            } else {
                default_ptr.reset(new Value(type, default_str));
            }
        }
        rich_header_.add_sure(RichAttr(name, type, default_ptr.get()));
    }
    InitHeader();
}


RelVar::RelVar(const Meta& meta,
               const string& name,
               const RichHeader& rich_header,
               const UniqueKeySet& unique_key_set,
               const ForeignKeySet& foreign_key_set,
               const Strings& checks)
    : name_(name)
    , rich_header_(rich_header)
    , unique_key_set_(unique_key_set)
    , foreign_key_set_(foreign_key_set)
{
    CheckName(name);
    CheckAttrNumber(rich_header.size());
    BOOST_FOREACH(const RichAttr& rich_attr, rich_header)
        CheckName(rich_attr.GetName());
    InitHeader();
    if (unique_key_set.empty() && !rich_header.empty()) {
        StringSet unique_key;
        unique_key.reserve(rich_header.size());
        BOOST_FOREACH(const RichAttr& rich_attr, rich_header)
            unique_key.add_sure(rich_attr.GetName());
        unique_key_set_.add_sure(unique_key);
    }

    ostringstream oss;

    static const format create_sequence_cmd(
        "CREATE SEQUENCE \"%1%@%2%\" MINVALUE 0 START 0;");
    BOOST_FOREACH(const RichAttr& rich_attr, rich_header)
        if (rich_attr.GetType() == Type::SERIAL)
            oss << (format(create_sequence_cmd) % name % rich_attr.GetName());

    oss << "CREATE TABLE \"" << name << "\" (";
    Separator sep;

    BOOST_FOREACH(const RichAttr& rich_attr, rich_header) {
        oss << sep << '"' << rich_attr.GetName() << "\" "
            << rich_attr.GetType().GetPgName() << " NOT NULL";
        const Value* default_ptr(rich_attr.GetDefaultPtr());
        if (default_ptr)
            oss << " DEFAULT " << default_ptr->GetPgLiter();
        else if (rich_attr.GetType() == Type::SERIAL)
            oss << " DEFAULT nextval('\""
                << name << '@' << rich_attr.GetName()
                << "\"')";
    }

    BOOST_FOREACH(const StringSet& unique_key, unique_key_set_) {
        oss << sep;
        PrintUniqueKey(oss, unique_key);
    }

    BOOST_FOREACH(const ForeignKey& foreign_key, foreign_key_set) {
        oss << sep;
        PrintForeignKey(oss, meta, foreign_key);
    }

    BOOST_FOREACH(const string& check, checks) {
        oss << sep;
        PrintCheck(oss, check);
    }

    oss << ");";

    static const format alter_sequence_cmd(
        "ALTER SEQUENCE \"%1%@%2%\" OWNED BY \"%1%\".\"%2%\";");
    BOOST_FOREACH(const RichAttr& rich_attr, rich_header)
        if (rich_attr.GetType() == Type::SERIAL)
            oss << (format(alter_sequence_cmd) % name % rich_attr.GetName());

    Exec(oss.str());
}


void RelVar::LoadConstrs(const Meta& meta) {
    static const format query("SELECT * FROM ak.describe_constrs('\"%1%\"')");
    pqxx::result pqxx_result = Exec((format(query) % name_).str());
    BOOST_FOREACH(const pqxx::result::tuple& tuple, pqxx_result) {
        AK_ASSERT_EQUAL(tuple.size(), 4U);
        AK_ASSERT(!tuple[0].is_null() && !tuple[1].is_null());
        AK_ASSERT_EQUAL(string(tuple[0].c_str()).size(), 1U);
        StringSet attr_names(ReadAttrNames(tuple[1].c_str()));
        char constr_code = tuple[0].c_str()[0];
        if (constr_code == 'p' || constr_code == 'u') {
            unique_key_set_.add_sure(attr_names);
        } else if (constr_code == 'f') {
            AK_ASSERT(!tuple[2].is_null() && !tuple[3].is_null());
            string ref_rel_var_name(tuple[2].c_str());
            StringSet ref_attr_names(
                meta.Get(ref_rel_var_name).ReadAttrNames(tuple[3].c_str()));
            foreign_key_set_.add_sure(ForeignKey(attr_names,
                                                 ref_rel_var_name,
                                                 ref_attr_names));
        };
    }
}


bool RelVar::Intersect(const StringSet& lhs, const StringSet& rhs)
{
    BOOST_FOREACH(const string& str, lhs)
        if (rhs.contains(str))
            return true;
    return false;
}


void RelVar::CheckName(const string& name)
{
    if (name.empty())
        throw Error(Error::VALUE, "Identifier can't be empty");
    if (name.size() > MAX_NAME_SIZE) {
        static const string message(
            (format("RelVar and attribute name length must be "
                    "no more than %1% characters") %
             MAX_NAME_SIZE).str());
        throw Error(Error::QUOTA, message);
    }
    const locale& loc(locale::classic());
    if (name[0] != '_' && !isalpha(name[0], loc))
        throw Error(Error::VALUE,
                    ("First identifier character must be "
                     "a letter or underscore"));
    for (size_t i = 1; i < name.size(); ++i)
        if (name[i] != '_' && !isalnum(name[i], loc))
            throw Error(Error::VALUE,
                        ("Identifier must consist only of "
                         "letters, digits or underscores"));
}


void RelVar::CheckAttrNumber(size_t number)
{
    if (number > MAX_ATTR_NUMBER) {
        static const string message(
            (format("Maximum attribute number is %1%") %
             MAX_ATTR_NUMBER).str());
        throw Error(Error::QUOTA, message);
    }
}


void RelVar::PrintAttrNames(ostream& os, const StringSet& names) {
    Separator sep;
    os << '(';
    BOOST_FOREACH(const string& name, names)
        os << sep << '"' << name << '"';
    os << ')';
}


StringSet RelVar::ReadAttrNames(const string& pg_array) const
{
    AK_ASSERT(pg_array.size() >= 2);
    istringstream iss(pg_array.substr(1, pg_array.size() - 2));
    StringSet result;
    for (;;) {
        size_t index;
        iss >> index;
        AK_ASSERT(!iss.fail());
        result.add_sure(rich_header_[index - 1].GetName());
        if (iss.eof())
            return result;
        char comma;
        iss.get(comma);
        AK_ASSERT_EQUAL(comma, ',');
    }
}


void RelVar::PrintUniqueKey(ostream& os, const StringSet& unique_key) const
{
    if (unique_key.empty())
        throw Error(Error::VALUE, "Empty unique attribute set");
    BOOST_FOREACH(const string& attr_name, unique_key)
        rich_header_.find(attr_name);
    os << "UNIQUE ";
    PrintAttrNames(os, unique_key);
}


void RelVar::PrintForeignKey(ostream& os,
                             const Meta& meta,
                             const ForeignKey& foreign_key) const
{
    if (foreign_key.key_attr_names.size() !=
        foreign_key.ref_attr_names.size())
        throw Error(Error::VALUE, "Ref-key attribute set size mismatch");
    if (foreign_key.key_attr_names.empty())
        throw Error(Error::VALUE,
                    "Foreign key with empty attribute set");
    const RelVar& ref_rel_var(foreign_key.ref_rel_var_name == name_
                              ? *this
                              : meta.Get(foreign_key.ref_rel_var_name));
    const RichHeader& ref_rich_header(ref_rel_var.GetRichHeader());
    for (size_t i = 0; i < foreign_key.key_attr_names.size(); ++i) {
        string key_attr_name = foreign_key.key_attr_names[i];
        string ref_attr_name = foreign_key.ref_attr_names[i];
        RichAttr key_rich_attr(rich_header_.find(key_attr_name));
        RichAttr ref_rich_attr(ref_rich_header.find(ref_attr_name));
        Type key_attr_type(key_rich_attr.GetType());
        Type ref_attr_type(ref_rich_attr.GetType());
        if (key_attr_type != ref_attr_type &&
            !((key_attr_type == Type::INT && ref_attr_type == Type::SERIAL) ||
              (ref_attr_type == Type::INT && key_attr_type == Type::SERIAL)))
            throw Error(Error::USAGE,
                        ("Foreign key attribite type mismatch: \"" +
                         name_ + '.' +
                         key_attr_name + "\" is " +
                         key_attr_type.GetName() +
                         ", \"" +
                         foreign_key.ref_rel_var_name + '.' +
                         ref_attr_name + "\" is " +
                         ref_attr_type.GetName()));
    }
    bool unique_found = false;
    BOOST_FOREACH(const StringSet& unique_key,
                  ref_rel_var.GetUniqueKeySet()) {
        if (unique_key == foreign_key.ref_attr_names) {
            unique_found = true;
            break;
        }
    }
    if (!unique_found)
        throw Error(Error::USAGE,
                    "Foreign key ref attributes must be unique");
    os << "FOREIGN KEY ";
    PrintAttrNames(os, foreign_key.key_attr_names);
    os << " REFERENCES \"" << foreign_key.ref_rel_var_name << '"';
    PrintAttrNames(os, foreign_key.ref_attr_names);
}


void RelVar::PrintCheck(ostream& os, const string& check) const
{
    os << "CHECK (" << TranslateExpr(check, name_, header_) << ')';
}


string RelVar::GetName() const
{
    return name_;
}


const RichHeader& RelVar::GetRichHeader() const
{
    return rich_header_;
}


const Header& RelVar::GetHeader() const
{
    return header_;
}


const UniqueKeySet& RelVar::GetUniqueKeySet() const
{
    return unique_key_set_;
}


const ForeignKeySet& RelVar::GetForeignKeySet() const
{
    return foreign_key_set_;
}


void RelVar::InitHeader()
{
    header_.reserve(rich_header_.size());
    BOOST_FOREACH(const RichAttr& rich_attr, rich_header_)
        header_.add_sure(rich_attr.GetAttr());
}


void RelVar::AddAttrs(const RichHeader& rich_attrs)
{
    if (rich_attrs.empty())
        return;
    CheckAttrNumber(rich_header_.size() + rich_attrs.size());
    ostringstream oss;
    oss << "ALTER TABLE \"" << name_ << "\" ";
    Separator sep;
    BOOST_FOREACH(const RichAttr& rich_attr, rich_attrs) {
        string attr_name(rich_attr.GetName());
        CheckName(attr_name);
        BOOST_FOREACH(const RichAttr& old_rich_attr, rich_header_)
            if (old_rich_attr.GetName() == attr_name)
                throw Error(Error::ATTR_EXISTS,
                            "Attribute \"" + attr_name + "\" already exists");
        oss << sep << "ADD \"" << attr_name << "\" "
            << rich_attr.GetType().GetPgName();
    }
    oss << "; UPDATE \"" << name_ << "\" SET ";
    sep = Separator();
    BOOST_FOREACH(const RichAttr& rich_attr, rich_attrs)
        oss << sep << '"' << rich_attr.GetName() << "\" = "
            << rich_attr.GetDefaultPtr()->GetPgLiter();
    oss << "; ALTER TABLE \"" << name_ << "\" ";
    sep = Separator();
    BOOST_FOREACH(const RichAttr& rich_attr, rich_attrs)
        oss << sep << "ALTER \"" << rich_attr.GetName() << "\" SET NOT NULL";
    StringSet unique_key;
    if (rich_header_.empty()) {
        oss << ", ADD UNIQUE (";
        Separator sep;
        BOOST_FOREACH(const RichAttr& rich_attr, rich_attrs) {
            string attr_name(rich_attr.GetName());
            unique_key.add_sure(attr_name);
            oss << sep <<'"' << attr_name << '"';
        }
        oss << ')';
    }
    Exec(oss.str());
    BOOST_FOREACH(const RichAttr& rich_attr, rich_attrs) {
        rich_header_.add_sure(
            RichAttr(rich_attr.GetName(), rich_attr.GetType()));
        header_.add_sure(rich_attr.GetAttr());
    }
    if (!unique_key.empty())
        unique_key_set_.add_sure(unique_key);
}


void RelVar::DropAttrs(const StringSet& attr_names)
{
    if (attr_names.empty())
        return;
    UniqueKeySet new_unique_key_set;
    BOOST_FOREACH(const StringSet& unique_key, unique_key_set_)
        if (!Intersect(unique_key, attr_names))
            new_unique_key_set.add_sure(unique_key);
    ForeignKeySet new_foreign_key_set;
    BOOST_FOREACH(const ForeignKey& foreign_key, foreign_key_set_)
        if (!Intersect(foreign_key.key_attr_names, attr_names))
            new_foreign_key_set.add_sure(foreign_key);
    StringSet remaining_attr_names(attr_names);
    RichHeader new_rich_header;
    BOOST_FOREACH(const RichAttr& rich_attr, rich_header_) {
        bool found = false;
        for (size_t i = 0; i < remaining_attr_names.size(); ++i) {
            if (remaining_attr_names[i] == rich_attr.GetName()) {
                remaining_attr_names.erase(remaining_attr_names.begin() + i);
                found = true;
                break;
            }
        }
        if (!found)
            new_rich_header.add_sure(rich_attr);
    }
    if (!remaining_attr_names.empty())
        throw Error(
            Error::NO_SUCH_ATTR,
            "Attribute \"" + remaining_attr_names[0] + "\" does not exist");
    ostringstream oss;
    oss << "ALTER TABLE \"" << name_ << "\" ";
    Separator sep;
    BOOST_FOREACH(const string& attr_name, attr_names)
        oss << sep << "DROP \"" << attr_name << '"';
    if (new_unique_key_set.empty() && !new_rich_header.empty()) {
        oss << ", ADD UNIQUE (";
        StringSet unique_key;
        Separator sep;
        BOOST_FOREACH(const RichAttr& new_rich_attr, new_rich_header) {
            string new_attr_name(new_rich_attr.GetName());
            unique_key.add_sure(new_attr_name);
            oss << sep << '"' << new_attr_name << '"';
        }
        new_unique_key_set.add_sure(unique_key);
        oss << ')';
    }
    try {
        ExecSafely(oss.str());
    } catch (const pqxx::unique_violation& err) {
        throw Error(Error::CONSTRAINT,
                    ("Cannot drop attributes because "
                     "remaining tuples have duplicates"));
    } catch (const pqxx::sql_error& err) {
        throw Error(Error::REL_VAR_DEPENDENCY,
                    ("Cannot drop attribute because "
                     "it is referenced from other relation variable"));
    }
    unique_key_set_ = new_unique_key_set;
    foreign_key_set_ = new_foreign_key_set;
    rich_header_ = new_rich_header;
    header_.clear();
    InitHeader();
}


void RelVar::AddDefault(const DraftMap& draft_map)
{
    if (draft_map.empty())
        return;
    RichHeader new_rich_header(rich_header_);
    ostringstream oss;
    oss << "ALTER TABLE \"" << name_ << "\" ";
    Separator sep;
    BOOST_FOREACH(const DraftMap::value_type& named_draft, draft_map) {
        RichAttr& new_rich_attr(new_rich_header.find(named_draft.first));
        const Value& value(named_draft.second.Get(new_rich_attr.GetType()));
        new_rich_attr.SetDefaultPtr(&value);
        oss << sep << "ALTER \"" << named_draft.first
            << "\" SET DEFAULT " <<  value.GetPgLiter();
    }
    Exec(oss.str());
    rich_header_ = new_rich_header;
}


void RelVar::DropDefault(const StringSet& attr_names)
{
    if (attr_names.empty())
        return;
    vector<RichAttr*> rich_attr_ptrs;
    rich_attr_ptrs.reserve(attr_names.size());
    ostringstream oss;
    oss << "ALTER TABLE \"" << name_ << "\" ";
    Separator sep;
    BOOST_FOREACH(const string& attr_name, attr_names) {
        RichAttr& rich_attr(rich_header_.find(attr_name));
        if (!rich_attr.GetDefaultPtr())
            throw Error(Error::DB,
                        "Attribute \"" + attr_name + "\" has no default value");
        rich_attr_ptrs.push_back(&rich_attr);
        oss << sep << "ALTER \"" << attr_name << "\" DROP DEFAULT";
    }
    Exec(oss.str());
    BOOST_FOREACH(RichAttr* rich_attr_ptr, rich_attr_ptrs)
        rich_attr_ptr->SetDefaultPtr(0);
}


void RelVar::AddConstrs(const Meta& meta,
                        const UniqueKeySet& unique_key_set,
                        const ForeignKeySet& foreign_key_set,
                        const Strings& checks)
{
    if (unique_key_set.empty() && foreign_key_set.empty() && checks.empty())
        return;
    ostringstream oss;
    oss << "ALTER TABLE \"" << name_ << "\" ";
    Separator sep;
    BOOST_FOREACH(const StringSet& unique_key, unique_key_set) {
        oss << sep << "ADD ";
        PrintUniqueKey(oss, unique_key);
    }
    BOOST_FOREACH(const ForeignKey& foreign_key, foreign_key_set) {
        oss << sep << "ADD ";
        PrintForeignKey(oss, meta, foreign_key);
    }
    BOOST_FOREACH(const string& check, checks) {
        oss << sep << "ADD ";
        PrintCheck(oss, check);
    }
    try {
        ExecSafely(oss.str());
    } catch (const pqxx::unique_violation& err) {
        throw Error(Error::CONSTRAINT,
                    "Unique constraint cannot be added");
    } catch (const pqxx::foreign_key_violation& err) {
        throw Error(Error::CONSTRAINT,
                    "Foreign key constraint cannot be added");
    } catch (const pqxx::check_violation& err) {
        throw Error(Error::CONSTRAINT,
                    "Check constraint cannot be added");
    }
    BOOST_FOREACH(const StringSet& unique_key, unique_key_set)
        unique_key_set_.add_unsure(unique_key);
    BOOST_FOREACH(const ForeignKey& foreign_key, foreign_key_set)
        foreign_key_set_.add_unsure(foreign_key);
}


void RelVar::DropAllConstrs()
{
    if (rich_header_.empty())
        return;
    ostringstream oss;
    oss << "SELECT ak.drop_all_constrs('" << name_ << "'); "
        << "ALTER TABLE \"" << name_ << "\" ADD UNIQUE (";
    StringSet unique_key;
    Separator sep;
    BOOST_FOREACH(const RichAttr& rich_attr, rich_header_) {
        string attr_name(rich_attr.GetName());
        unique_key.add_sure(attr_name);
        oss << sep << '"' << attr_name << '"';
    }
    oss << ')';
    try {
        ExecSafely(oss.str());
    } catch (const pqxx::sql_error& err) {
        throw (string(err.what()).substr(0, 13) == "ERROR:  index"
               ? Error(Error::QUOTA, "Unique string is too long")
               : Error(Error::REL_VAR_DEPENDENCY,
                       ("Unique cannot be dropped "
                        "because other RelVar references it")));
    }
    unique_key_set_.clear();
    unique_key_set_.add_sure(unique_key);
    foreign_key_set_.clear();
}

////////////////////////////////////////////////////////////////////////////////
// Meta definitions
////////////////////////////////////////////////////////////////////////////////

Meta::Meta(const string& schema_name)
{
    static const format query("SELECT * FROM ak.get_schema_tables('%1%');");
    pqxx::result pqxx_result = Exec((format(query) % schema_name).str());
    rel_vars_.reserve(pqxx_result.size());
    BOOST_FOREACH(const pqxx::result::tuple& tuple, pqxx_result) {
        AK_ASSERT_EQUAL(tuple.size(), 1U);
        AK_ASSERT(!tuple[0].is_null());
        rel_vars_.push_back(RelVar(tuple[0].c_str()));
    }
    BOOST_FOREACH(RelVar& rel_var, rel_vars_)
        rel_var.LoadConstrs(*this);
}


const RelVars& Meta::GetAll() const
{
    return rel_vars_;
}


const RelVar& Meta::Get(const string& rel_var_name) const
{
    return rel_vars_[GetIdxChecked(rel_var_name)];
}


RelVar& Meta::Get(const string& rel_var_name)
{
    return rel_vars_[GetIdxChecked(rel_var_name)];
}


void Meta::Create(const string& rel_var_name,
                  const RichHeader& rich_header,
                  const UniqueKeySet& unique_key_set,
                  const ForeignKeySet& foreign_key_set,
                  const Strings& checks)
{
    if (rel_vars_.size() >= MAX_REL_VAR_NUMBER) {
        static const string message(
            (format("Maximum RelVar number is %1%") %
             MAX_REL_VAR_NUMBER).str());
        throw Error(Error::QUOTA, message);
    }
    if (GetIdx(rel_var_name) != MINUS_ONE)
        throw Error(Error::REL_VAR_EXISTS,
                    "RelVar \"" + rel_var_name + "\" already exists");
    rel_vars_.push_back(RelVar(*this,
                               rel_var_name,
                               rich_header,
                               unique_key_set,
                               foreign_key_set,
                               checks));
}


void Meta::Drop(const StringSet& rel_var_names)
{
    if (rel_var_names.empty())
        return;
    vector<size_t> indexes(rel_var_names.size(), -1);
    for (size_t i = 0; i < rel_vars_.size(); ++i) {
        const RelVar& rel_var(rel_vars_[i]);
        const string* name_ptr = rel_var_names.find_ptr(rel_var.GetName());
        if (name_ptr)
            indexes[name_ptr - &rel_var_names[0]] = i;
        else
            BOOST_FOREACH(const ForeignKey& foreign_key,
                          rel_var.GetForeignKeySet())
                if (rel_var_names.contains(foreign_key.ref_rel_var_name))
                    throw Error(Error::REL_VAR_DEPENDENCY,
                                ("Attempt to delete a group of RelVars "
                                 "with a RelVar \"" +
                                 foreign_key.ref_rel_var_name +
                                 "\" but without a RelVar \"" +
                                 rel_var.GetName() +
                                 "\" it is dependent on"));
    }
    vector<size_t>::const_iterator index_itr(
        find(indexes.begin(), indexes.end(), -1));
    if (index_itr != indexes.end())
        throw Error(Error::NO_SUCH_REL_VAR,
                    ("No such RelVar: \"" +
                     rel_var_names[index_itr - indexes.begin()] + '"'));
    ostringstream oss;
    oss << "DROP TABLE ";
    Separator sep;
    BOOST_FOREACH(const string& rel_var_name, rel_var_names)
        oss << sep << '"' << rel_var_name << '"';
    oss << " CASCADE";
    Exec(oss.str());
    sort(indexes.begin(), indexes.end());
    BOOST_REVERSE_FOREACH(size_t index, indexes)
        rel_vars_.erase(rel_vars_.begin() + index);
}


size_t Meta::GetIdx(const string& rel_var_name) const
{
    for (size_t i = 0; i < rel_vars_.size(); ++i)
        if (rel_vars_[i].GetName() == rel_var_name)
            return i;
    return -1;
}


size_t Meta::GetIdxChecked(const string& rel_var_name) const
{
    size_t idx = GetIdx(rel_var_name);
    if (idx == MINUS_ONE)
        throw Error(Error::NO_SUCH_REL_VAR,
                    "No such RelVar: \"" + rel_var_name + '"');
    return idx;
}

////////////////////////////////////////////////////////////////////////////////
// DB
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class DB {
    public:
        DB(const string& options,
           const string& schema_name,
           const string& tablespace_name);

        const Meta& GetMeta();
        Meta& ChangeMeta();
        string Quote(const string& str);
        pqxx::result Exec(const string& sql);
        pqxx::result ExecSafely(const string& sql);
        void Commit();
        void RollBack();

    private:
        pqxx::connection conn_;
        string schema_name_;
        auto_ptr<Meta> meta_ptr_;
        bool meta_changed_;
        auto_ptr<pqxx::work> work_ptr_;

        pqxx::work& GetWork();
    };
}


DB::DB(const string& options,
       const string& schema_name,
       const string& tablespace_name)
    : conn_(options)
    , schema_name_(schema_name)
{
    static const format cmd("SET search_path TO %1%, pg_catalog;"
                            "SET default_tablespace TO %2%;");
    conn_.set_noticer(auto_ptr<pqxx::noticer>(new pqxx::nonnoticer()));
    Exec((format(cmd) % Quote(schema_name) % Quote(tablespace_name)).str());
    Commit();
}


const Meta& DB::GetMeta()
{
    if (!meta_ptr_.get()) {
        meta_changed_ = false;
        meta_ptr_.reset(new Meta(schema_name_));
    }
    return *meta_ptr_;
}


Meta& DB::ChangeMeta()
{
    meta_changed_ = true;
    if (!meta_ptr_.get())
        meta_ptr_.reset(new Meta(schema_name_));
    return *meta_ptr_;
}


string DB::Quote(const string& str)
{
    return conn_.quote(str);
}


pqxx::work& DB::GetWork()
{
    if (!work_ptr_.get())
        work_ptr_.reset(new pqxx::work(conn_));
    return *work_ptr_;
}


pqxx::result DB::Exec(const string& sql)
{
    return GetWork().exec(sql);
}


pqxx::result DB::ExecSafely(const string& sql)
{
    return pqxx::subtransaction(GetWork()).exec(sql);
}


void DB::Commit()
{
    if (work_ptr_.get()) {
        work_ptr_->commit();
        work_ptr_.reset();
    }
    meta_changed_ = false;
}


void DB::RollBack()
{
    work_ptr_.reset();
    if (meta_changed_)
        meta_ptr_.reset();
}

////////////////////////////////////////////////////////////////////////////////
// db_ptr and callbacks
////////////////////////////////////////////////////////////////////////////////

namespace
{
    DB* db_ptr = 0;


    pqxx::result Exec(const string& sql)
    {
        return db_ptr->Exec(sql);
    }


    pqxx::result ExecSafely(const string& sql)
    {
        return db_ptr->ExecSafely(sql);
    }


    string Quote(const string& str)
    {
        return db_ptr->Quote(str);
    }


    const Header& GetHeader(const string& rel_var_name)
    {
        return db_ptr->GetMeta().Get(rel_var_name).GetHeader();
    }


    void FollowReference(const std::string& key_rel_var_name,
                         const StringSet& key_attr_names,
                         std::string& ref_rel_var_name,
                         StringSet& ref_attr_names)
    {
        const RelVar& rel_var(db_ptr->GetMeta().Get(key_rel_var_name));
        const ForeignKey* foreign_key_ptr = 0;
        BOOST_FOREACH(const ForeignKey& foreign_key,
                      rel_var.GetForeignKeySet()) {
            if (foreign_key.key_attr_names == key_attr_names) {
                if (foreign_key_ptr)
                    throw Error(Error::QUERY, "Multiple foreign keys");
                else
                    foreign_key_ptr = &foreign_key;
            }
        }
        if (!foreign_key_ptr)
            throw Error(Error::QUERY, "Foreign key not found");
        ref_rel_var_name = foreign_key_ptr->ref_rel_var_name;
        ref_attr_names = foreign_key_ptr->ref_attr_names;
    }
}

////////////////////////////////////////////////////////////////////////////////
// API
////////////////////////////////////////////////////////////////////////////////

namespace
{
    Values GetTupleValues(const pqxx::result::tuple& tuple,
                          const Header& header)
    {
        AK_ASSERT_EQUAL(tuple.size(), header.size());
        Values result;
        result.reserve(tuple.size());
        for (size_t i = 0; i < tuple.size(); ++i) {
            pqxx::result::field field(tuple[i]);
            Type type(header[i].GetType());
            if (type == Type::NUMBER)
                result.push_back(Value(type, field.as<double>()));
            else if (type == Type::BOOL)
                result.push_back(Value(type, field.as<bool>()));
            else
                result.push_back(Value(type, field.as<string>()));
        }
        return result;
    }
}


void ak::Commit()
{
    db_ptr->Commit();
}


void ak::RollBack()
{
    db_ptr->RollBack();
}


StringSet ak::GetRelVarNames()
{
    const RelVars& rel_vars(db_ptr->GetMeta().GetAll());
    StringSet result;
    result.reserve(rel_vars.size());
    BOOST_FOREACH(const RelVar& rel_var, rel_vars)
        result.add_sure(rel_var.GetName());
    return result;
}


const RichHeader& ak::GetRichHeader(const string& rel_var_name)
{
    return db_ptr->GetMeta().Get(rel_var_name).GetRichHeader();
}


const UniqueKeySet& ak::GetUniqueKeySet(const string& rel_var_name)
{
    return db_ptr->GetMeta().Get(rel_var_name).GetUniqueKeySet();
}


const ForeignKeySet& ak::GetForeignKeySet(const string& rel_var_name)
{
    return db_ptr->GetMeta().Get(rel_var_name).GetForeignKeySet();
}


void ak::CreateRelVar(const string& name,
                      const RichHeader& rich_header,
                      const UniqueKeySet& unique_key_set,
                      const ForeignKeySet& foreign_key_set,
                      const Strings& checks)
{
    db_ptr->ChangeMeta().Create(
        name, rich_header, unique_key_set, foreign_key_set, checks);
}


void ak::DropRelVars(const StringSet& rel_var_names)
{
    db_ptr->ChangeMeta().Drop(rel_var_names);
}


void ak::Query(Header& header,
               vector<Values>& tuples,
               const string& query,
               const Drafts& query_params,
               const Strings& by_exprs,
               const Drafts& by_params,
               size_t start,
               size_t length)
{
    string sql(
        TranslateQuery(
            header, query, query_params, by_exprs, by_params, start, length));
    pqxx::result pqxx_result(Exec(sql));
    tuples.clear();
    if (header.empty()) {
        if (!pqxx_result.empty())
            tuples.push_back(Values());
    } else {
        tuples.reserve(pqxx_result.size());
        BOOST_FOREACH(const pqxx::result::tuple& pqxx_tuple, pqxx_result)
            tuples.push_back(GetTupleValues(pqxx_tuple, header));
    }
}


size_t ak::Count(const string& query, const Drafts& params)
{
    string sql(TranslateCount(query, params));
    pqxx::result pqxx_result(Exec(sql));
    AK_ASSERT_EQUAL(pqxx_result.size(), 1U);
    AK_ASSERT_EQUAL(pqxx_result[0].size(), 1U);
    return pqxx_result[0][0].as<size_t>();
}


size_t ak::Update(const string& rel_var_name,
                  const string& where,
                  const Drafts& where_params,
                  const StringMap& expr_map,
                  const Drafts& expr_params)
{
    const RichHeader& rich_header(GetRichHeader(rel_var_name));
    uint64_t size = 0;
    BOOST_FOREACH(const StringMap::value_type& named_expr, expr_map) {
        rich_header.find(named_expr.first);
        // FIXME it's wrong estimation for expressions
        size += named_expr.second.size();
    }
    string sql(
        TranslateUpdate(
            rel_var_name, where, where_params, expr_map, expr_params));
    try{
        return ExecSafely(sql).affected_rows();
    } catch (const pqxx::integrity_constraint_violation& err) {
        throw Error(Error::CONSTRAINT, err.what());
    } catch (const pqxx::data_exception& err) {
        throw Error(Error::CONSTRAINT, err.what());
    } catch (const pqxx::sql_error& err) {
        throw Error(Error::DB, err.what());
    }
}


size_t ak::Delete(const string& rel_var_name,
                  const string& where,
                  const Drafts& params)
{
    string sql(TranslateDelete(rel_var_name, where, params));
    try {
        return ExecSafely(sql).affected_rows();
    } catch (const pqxx::integrity_constraint_violation& err) {
        throw Error(Error::CONSTRAINT, err.what());
    } catch (const pqxx::sql_error& err) {
        throw Error(Error::DB, err.what());
    }
}


Values ak::Insert(const string& rel_var_name, const DraftMap& draft_map)
{
    static const format empty_cmd("SELECT ak.insert_into_empty('%1%');");
    static const format cmd(
        "INSERT INTO \"%1%\" (%2%) VALUES (%3%) RETURNING *;");
    static const format default_cmd(
        "INSERT INTO \"%1%\" DEFAULT VALUES RETURNING *;");

    const RelVar& rel_var(db_ptr->GetMeta().Get(rel_var_name));
    const RichHeader& rich_header(rel_var.GetRichHeader());
    string sql;
    uint64_t size = 0;
    if (rich_header.empty()) {
        if (!draft_map.empty())
            rich_header.find(draft_map.begin()->first); // throws
        sql = (format(empty_cmd) % rel_var_name).str();
    } else {
        if (!draft_map.empty()) {
            BOOST_FOREACH(const DraftMap::value_type& named_draft, draft_map)
                rich_header.find(named_draft.first);
            ostringstream names_oss, values_oss;
            Separator names_sep, values_sep;
            BOOST_FOREACH(const RichAttr& rich_attr, rich_header) {
                DraftMap::const_iterator itr(
                    draft_map.find(rich_attr.GetName()));
                if (itr == draft_map.end()) {
                    if (rich_attr.GetType() != Type::SERIAL &&
                        !rich_attr.GetDefaultPtr())
                        throw Error(Error::ATTR_VALUE_REQUIRED,
                                    ("Value of attribute \"" +
                                     rich_attr.GetName() +
                                     "\" must be supplied"));
                } else {
                    names_oss << names_sep << '"' << rich_attr.GetName() << '"';
                    Value value(itr->second.Get(rich_attr.GetType()));
                    values_oss << values_sep << value.GetPgLiter();
                }
            }
            sql = (format(cmd)
                   % rel_var_name
                   % names_oss.str()
                   % values_oss.str()).str();
            size += values_oss.str().size();
        } else {
            sql = (format(default_cmd) % rel_var_name).str();
        }
        BOOST_FOREACH(const RichAttr& rich_attr, rich_header) {
            const Value* default_ptr = rich_attr.GetDefaultPtr();
            if (default_ptr) {
                double d;
                string s;
                size += default_ptr->Get(d, s) ? s.size() : 16;
            }
        }
    }
    pqxx::result pqxx_result;
    try {
        pqxx_result = ExecSafely(sql);
    } catch (const pqxx::integrity_constraint_violation& err) {
        throw Error(Error::CONSTRAINT, err.what());
    } catch (const pqxx::data_exception& err) {
        throw Error(Error::CONSTRAINT, err.what());
    } catch (const pqxx::plpgsql_raise& err) {
        throw Error(Error::CONSTRAINT, err.what());
    } catch (const pqxx::sql_error& err) {
        throw Error(Error::DB, err.what());
    }
    if (rich_header.empty())
        return Values();
    AK_ASSERT_EQUAL(pqxx_result.size(), 1U);
    return GetTupleValues(pqxx_result[0], rel_var.GetHeader());
}


void ak::AddAttrs(const string& rel_var_name, const RichHeader& rich_attrs)
{
    db_ptr->ChangeMeta().Get(rel_var_name).AddAttrs(rich_attrs);
}


void ak::DropAttrs(const string& rel_var_name,
                   const StringSet& attr_names)
{
    db_ptr->ChangeMeta().Get(rel_var_name).DropAttrs(attr_names);
}


void ak::AddDefault(const string& rel_var_name, const DraftMap& draft_map)
{
    db_ptr->ChangeMeta().Get(rel_var_name).AddDefault(draft_map);
}


void ak::DropDefault(const string& rel_var_name,
                     const StringSet& attr_names)
{
    db_ptr->ChangeMeta().Get(rel_var_name).DropDefault(attr_names);
}


void ak::AddConstrs(const string& rel_var_name,
                    const UniqueKeySet& unique_key_set,
                    const ForeignKeySet& foreign_key_set,
                    const Strings& checks)
{
    Meta& meta(db_ptr->ChangeMeta());
    meta.Get(rel_var_name).AddConstrs(
        meta, unique_key_set, foreign_key_set, checks);
}


void ak::DropAllConstrs(const string& rel_var_name)
{
    db_ptr->ChangeMeta().Get(rel_var_name).DropAllConstrs();
}


void ak::InitDatabase(const string& options,
                      const string& schema_name,
                      const string& tablespace_name)
{
    AK_ASSERT(!db_ptr);
    static DB db(options, schema_name, tablespace_name);
    db_ptr = &db;
    InitCommon(Quote);
    InitTranslator(GetHeader, FollowReference);
}
