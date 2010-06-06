
// (c) 2008-2010 by Anton Korenyushkin

#include "db.h"
#include "translator.h"
#include "utils.h"

#include <pqxx/pqxx>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>


using namespace std;
using namespace ku;
using boost::format;
using boost::scoped_ptr;
using boost::static_visitor;
using boost::apply_visitor;
using boost::lexical_cast;
using boost::noncopyable;


////////////////////////////////////////////////////////////////////////////////
// Constants
////////////////////////////////////////////////////////////////////////////////

namespace
{
    const size_t MAX_NAME_SIZE = 60;
    const size_t MAX_ATTR_NUMBER = 500;
    const size_t MAX_REL_VAR_NUMBER = 500;
    const size_t MAX_STRING_SIZE = 1024 * 1024;
    const uint64_t QUOTA_MULTIPLICATOR = 1024 * 1024;
    
#ifdef TEST
    const size_t ADDED_SIZE_MULTIPLICATOR = 16;
    const size_t CHANGED_ROWS_COUNT_LIMIT = 10;
#else
    const size_t ADDED_SIZE_MULTIPLICATOR = 4;
    const size_t CHANGED_ROWS_COUNT_LIMIT = 10000;
#endif
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
// Quoter
///////////////////////////////////////////////////////////////////////////////

namespace
{
    class Quoter {
    public:
        explicit Quoter(pqxx::connection& conn)
            : conn_(conn) {}

        explicit Quoter(const pqxx::transaction_base& work)
            : conn_(work.conn()) {}

        string operator()(const PgLiter& pg_liter) const {
            return pg_liter.quote_me ? conn_.quote(pg_liter.str) : pg_liter.str;
        }

        string operator()(const Value& value) const {
            return (*this)(value.GetPgLiter());
        }

        Strings operator()(const Values& values) const {
            Strings result;
            result.reserve(values.size());
            for (Values::const_iterator itr = values.begin();
                 itr != values.end();
                 ++itr)
                result.push_back((*this)(*itr));
            return result;
        }

    private:
        pqxx::connection_base& conn_;
    };
}

////////////////////////////////////////////////////////////////////////////////
// RichAttr
///////////////////////////////////////////////////////////////////////////////

RichAttr::RichAttr(const string& name,
                   Type type,
                   Type::Trait trait,
                   const Value* default_ptr)
    : attr_(name, type)
    , trait_(trait)
{
    KU_ASSERT(GetType().IsApplicable(trait_));
    KU_ASSERT(!(trait_ == Type::SERIAL && default_ptr));
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


Type::Trait RichAttr::GetTrait() const
{
    return trait_;
}


const Value* RichAttr::GetDefaultPtr() const
{
    return default_ptr_.get();
}


void RichAttr::SetDefaultPtr(const Value* default_ptr)
{
    if (default_ptr) {
        KU_ASSERT(default_ptr->GetType() == GetType());
        default_ptr_.reset(new Value(*default_ptr));
    } else {
        default_ptr_.reset();
    }
}

////////////////////////////////////////////////////////////////////////////////
// RelVar and DBMeta declarations
///////////////////////////////////////////////////////////////////////////////

namespace
{
    class DBMeta;

    
    class RelVar {
    public:
        RelVar(pqxx::work& work, const string& name);

        RelVar(pqxx::work& work,
               const Translator& translator,
               const DBMeta& meta,
               const string& name,
               const RichHeader& rich_header,
               const UniqueKeySet& unique_key_set,
               const ForeignKeySet& foreign_key_set,
               const Strings& checks);

        void LoadConstrs(pqxx::work& work, const DBMeta& meta);
        string GetName() const;
        const RichHeader& GetRichHeader() const;
        const Header& GetHeader() const;
        const UniqueKeySet& GetUniqueKeySet() const;
        const ForeignKeySet& GetForeignKeySet() const;
        void AddAttrs(pqxx::work& work, const RichHeader& attrs);
        void DropAttrs(pqxx::work& work, const StringSet& attr_names);
        void AddDefault(pqxx::work& work, const DraftMap& draft_map);
        void DropDefault(pqxx::work& work, const StringSet& attr_names);

        void AddConstrs(pqxx::work& work,
                        const Translator& translator,
                        const DBMeta& meta,
                        const UniqueKeySet& unique_key_set,
                        const ForeignKeySet& foreign_key_set,
                        const Strings& checks);

        void DropAllConstrs(pqxx::work& work);

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
                             const DBMeta& meta,
                             const ForeignKey& foreign_key) const;

        void PrintCheck(ostream& os,
                        const Translator& translator,
                        const string& check) const;
        
        void InitHeader();
    };
    
    
    typedef vector<RelVar> RelVars;


    class DBMeta {
    public:
        DBMeta(pqxx::work& work, const string& schema_name);
        const RelVar& Get(const string& rel_var_name) const;
        RelVar& Get(const string& rel_var_name);
        const RelVars& GetAll() const;
        
        void Create(pqxx::work& work,
                    const Translator& translator,
                    const string& rel_var_name,
                    const RichHeader& rich_header,
                    const UniqueKeySet& unique_key_set,
                    const ForeignKeySet& foreign_key_set,
                    const Strings& checks);
        
        void Drop(pqxx::work& work, const StringSet& rel_var_names);

    private:
        RelVars rel_vars_;

        size_t GetIdx(const string& rel_var_name) const; // never throws
        size_t GetIdxChecked(const string& rel_var_name) const;
    };
}

////////////////////////////////////////////////////////////////////////////////
// RelVar definitions
////////////////////////////////////////////////////////////////////////////////

RelVar::RelVar(pqxx::work& work, const string& name)
    : name_(name)
{
    static const format query("SELECT * FROM ku.describe_table('\"%1%\"');");
    pqxx::result pqxx_result = work.exec((format(query) % name_).str());
    rich_header_.reserve(pqxx_result.size());
    BOOST_FOREACH(const pqxx::result::tuple& tuple, pqxx_result) {
        KU_ASSERT_EQUAL(tuple.size(), 3U);
        KU_ASSERT(!tuple[0].is_null() && ! tuple[1].is_null());
        string name(tuple[0].c_str());
        Type type(tuple[1].c_str());
        Type::Trait trait = Type::COMMON;
        auto_ptr<Value> default_ptr;
        if (string(tuple[1].c_str()) == "int4") {
            KU_ASSERT(type == Type::NUMBER);
            trait = Type::INTEGER;
        }
        if (!tuple[2].is_null()) {
            string default_str(tuple[2].c_str());
            if (default_str.substr(0, 8) == "nextval(") {
                KU_ASSERT(type == Type::NUMBER);
                KU_ASSERT(trait == Type::INTEGER);
                trait = Type::SERIAL;
            } else {
                default_ptr.reset(new Value(type, default_str));
            }
        }
        rich_header_.add_sure(RichAttr(name, type, trait, default_ptr.get()));
    }
    InitHeader();
}


RelVar::RelVar(pqxx::work& work,
               const Translator& translator,
               const DBMeta& meta,
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
        if (rich_attr.GetTrait() == Type::SERIAL)
            oss << (format(create_sequence_cmd) % name % rich_attr.GetName());

    oss << "CREATE TABLE \"" << name << "\" (";
    OmitInvoker print_sep((SepPrinter(oss)));

    BOOST_FOREACH(const RichAttr& rich_attr, rich_header) {
        print_sep();
        oss << Quoted(rich_attr.GetName()) << ' '
            << rich_attr.GetType().GetPgStr(rich_attr.GetTrait())
            << " NOT NULL";
        const Value* default_ptr(rich_attr.GetDefaultPtr());
        if (default_ptr)
            oss << " DEFAULT " << Quoter(work)(default_ptr->GetPgLiter());
        else if (rich_attr.GetTrait() == Type::SERIAL)
            oss << " DEFAULT nextval('\""
                << name << '@' << rich_attr.GetName()
                << "\"')";
        if (rich_attr.GetType() == Type::STRING)
            oss << " CHECK (bit_length("
                << Quoted(rich_attr.GetName())
                << ") <= " << 8 * MAX_STRING_SIZE
                << ')';
    }

    BOOST_FOREACH(const StringSet& unique_key, unique_key_set_) {
        print_sep();
        PrintUniqueKey(oss, unique_key);
    }

    BOOST_FOREACH(const ForeignKey& foreign_key, foreign_key_set) {
        print_sep();
        PrintForeignKey(oss, meta, foreign_key);
    }

    BOOST_FOREACH(const string& check, checks) {
        print_sep();
        PrintCheck(oss, translator, check);
    }

    oss << ");";

    static const format alter_sequence_cmd(
        "ALTER SEQUENCE \"%1%@%2%\" OWNED BY \"%1%\".\"%2%\";");
    BOOST_FOREACH(const RichAttr& rich_attr, rich_header)
        if (rich_attr.GetTrait() == Type::SERIAL)
            oss << (format(alter_sequence_cmd) % name % rich_attr.GetName());
    
    work.exec(oss.str());
}


void RelVar::LoadConstrs(pqxx::work& work, const DBMeta& meta) {
    static const format query("SELECT * FROM ku.describe_constrs('\"%1%\"')");
    pqxx::result pqxx_result = work.exec((format(query) % name_).str());
    BOOST_FOREACH(const pqxx::result::tuple& tuple, pqxx_result) {
        KU_ASSERT_EQUAL(tuple.size(), 4U);
        KU_ASSERT(!tuple[0].is_null() && !tuple[1].is_null());
        KU_ASSERT_EQUAL(string(tuple[0].c_str()).size(), 1U);
        StringSet attr_names(ReadAttrNames(tuple[1].c_str()));
        char constr_code = tuple[0].c_str()[0];
        if (constr_code == 'p' || constr_code == 'u') {
            unique_key_set_.add_sure(attr_names);
        } else if (constr_code == 'f') {
            KU_ASSERT(!tuple[2].is_null() && !tuple[3].is_null());
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
        throw Error(Error::DB_QUOTA, message);
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
        throw Error(Error::DB_QUOTA, message);
    }
}


void RelVar::PrintAttrNames(ostream& os, const StringSet& names) {
    OmitInvoker print_sep((SepPrinter(os)));
    os << '(';
    BOOST_FOREACH(const string& name, names) {
        print_sep();
        os << Quoted(name);
    }
    os << ')';
}


StringSet RelVar::ReadAttrNames(const string& pg_array) const
{
    KU_ASSERT(pg_array.size() >= 2);
    istringstream iss(pg_array.substr(1, pg_array.size() - 2));
    StringSet result;
    for (;;) {
        size_t index;
        iss >> index;
        KU_ASSERT(!iss.fail());
        result.add_sure(rich_header_[index - 1].GetName());
        if (iss.eof())
            return result;
        char comma;
        iss.get(comma);
        KU_ASSERT_EQUAL(comma, ',');
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
                             const DBMeta& meta,
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
        Type::Trait key_attr_trait(key_rich_attr.GetTrait());
        Type::Trait ref_attr_trait(ref_rich_attr.GetTrait());
        if (key_attr_type != ref_attr_type ||
            !Type::TraitsAreCompatible(key_attr_trait, ref_attr_trait))
            throw Error(Error::USAGE,
                        ("Foreign key attribite type mismatch: \"" +
                         name_ + '.' +
                         key_attr_name + "\" is " +
                         key_attr_type.GetKuStr(key_attr_trait) +
                         ", \"" +
                         foreign_key.ref_rel_var_name + '.' +
                         ref_attr_name + "\" is " +
                         ref_attr_type.GetKuStr(ref_attr_trait)));
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
    os << " REFERENCES " << Quoted(foreign_key.ref_rel_var_name);
    PrintAttrNames(os, foreign_key.ref_attr_names);
}


void RelVar::PrintCheck(ostream& os,
                        const Translator& translator,
                        const string& check) const
{
    os << "CHECK (" << translator.TranslateExpr(check, name_, header_) << ')';
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


void RelVar::AddAttrs(pqxx::work& work, const RichHeader& rich_attrs)
{
    if (rich_attrs.empty())
        return;
    CheckAttrNumber(rich_header_.size() + rich_attrs.size());
    ostringstream oss;
    oss << "ALTER TABLE " << Quoted(name_) << ' ';
    OmitInvoker print_sep((SepPrinter(oss)));
    BOOST_FOREACH(const RichAttr& rich_attr, rich_attrs) {
        string attr_name(rich_attr.GetName());
        CheckName(attr_name);
        BOOST_FOREACH(const RichAttr& old_rich_attr, rich_header_)
            if (old_rich_attr.GetName() == attr_name)
                throw Error(Error::ATTR_EXISTS,
                            "Attribute \"" + attr_name + "\" already exists");
        print_sep();
        oss << "ADD " << Quoted(attr_name) << ' '
            << rich_attr.GetType().GetPgStr(rich_attr.GetTrait());
    }
    oss << "; UPDATE " << Quoted(name_) << " SET ";
    print_sep = OmitInvoker(SepPrinter(oss));
    BOOST_FOREACH(const RichAttr& rich_attr, rich_attrs) {
        print_sep();
        oss << Quoted(rich_attr.GetName()) << " = "
            << Quoter(work)(rich_attr.GetDefaultPtr()->GetPgLiter());
    }
    oss << "; ALTER TABLE " << Quoted(name_) << ' ';
    print_sep = OmitInvoker(SepPrinter(oss));
    BOOST_FOREACH(const RichAttr& rich_attr, rich_attrs) {
        print_sep();
        oss << "ALTER " << Quoted(rich_attr.GetName()) << " SET NOT NULL";
    }
    StringSet unique_key;
    if (rich_header_.empty()) {
        oss << ", ADD UNIQUE (";
        OmitInvoker print_sep((SepPrinter(oss)));
        BOOST_FOREACH(const RichAttr& rich_attr, rich_attrs) {
            string attr_name(rich_attr.GetName());
            unique_key.add_sure(attr_name);
            print_sep();
            oss << Quoted(attr_name);
        }
        oss << ")";
    }
    work.exec(oss.str());
    BOOST_FOREACH(const RichAttr& rich_attr, rich_attrs) {
        rich_header_.add_sure(RichAttr(rich_attr.GetName(),
                                       rich_attr.GetType(),
                                       rich_attr.GetTrait()));
        header_.add_sure(rich_attr.GetAttr());
    }
    if (!unique_key.empty())
        unique_key_set_.add_sure(unique_key);
}


void RelVar::DropAttrs(pqxx::work& work, const StringSet& attr_names)
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
    oss << "ALTER TABLE " << Quoted(name_) << ' ';
    OmitInvoker print_sep((SepPrinter(oss)));
    BOOST_FOREACH(const string& attr_name, attr_names) {
        print_sep();
        oss << "DROP " << Quoted(attr_name);
    }
    if (new_unique_key_set.empty() && !new_rich_header.empty()) {
        oss << ", ADD UNIQUE (";
        StringSet unique_key;
        OmitInvoker print_sep((SepPrinter(oss)));
        BOOST_FOREACH(const RichAttr& new_rich_attr, new_rich_header) {
            string new_attr_name(new_rich_attr.GetName());
            unique_key.add_sure(new_attr_name);
            print_sep();
            oss << Quoted(new_attr_name);
        }
        new_unique_key_set.add_sure(unique_key);
        oss << ')';
    }
    try {
        pqxx::subtransaction(work).exec(oss.str());
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


void RelVar::AddDefault(pqxx::work& work, const DraftMap& draft_map)
{
    if (draft_map.empty())
        return;
    RichHeader new_rich_header(rich_header_);
    ostringstream oss;
    oss << "ALTER TABLE " << Quoted(name_) << ' ';
    OmitInvoker print_sep((SepPrinter(oss)));
    BOOST_FOREACH(const DraftMap::value_type& named_draft, draft_map) {
        RichAttr& new_rich_attr(new_rich_header.find(named_draft.first));
        const Value& value(named_draft.second.Get(new_rich_attr.GetType()));
        new_rich_attr.SetDefaultPtr(&value);
        print_sep();
        oss << "ALTER " << Quoted(named_draft.first)
            << " SET DEFAULT " <<  Quoter(work)(value.GetPgLiter());
    }
    work.exec(oss.str());
    rich_header_ = new_rich_header;
}


void RelVar::DropDefault(pqxx::work& work, const StringSet& attr_names)
{
    if (attr_names.empty())
        return;
    vector<RichAttr*> rich_attr_ptrs;
    rich_attr_ptrs.reserve(attr_names.size());
    ostringstream oss;
    oss << "ALTER TABLE " << Quoted(name_) << ' ';
    OmitInvoker print_sep((SepPrinter(oss)));
    BOOST_FOREACH(const string& attr_name, attr_names) {
        RichAttr& rich_attr(rich_header_.find(attr_name));
        if (!rich_attr.GetDefaultPtr())
            throw Error(Error::DB,
                        "Attribute \"" + attr_name + "\" has no default value");
        rich_attr_ptrs.push_back(&rich_attr);
        print_sep();
        oss << "ALTER " << Quoted(attr_name) << " DROP DEFAULT";
    }
    work.exec(oss.str());
    BOOST_FOREACH(RichAttr* rich_attr_ptr, rich_attr_ptrs)
        rich_attr_ptr->SetDefaultPtr(0);
}


void RelVar::AddConstrs(pqxx::work& work,
                        const Translator& translator,
                        const DBMeta& meta,
                        const UniqueKeySet& unique_key_set,
                        const ForeignKeySet& foreign_key_set,
                        const Strings& checks)
{
    if (unique_key_set.empty() && foreign_key_set.empty() && checks.empty())
        return;
    ostringstream oss;
    oss << "ALTER TABLE " << Quoted(name_) << ' ';
    OmitInvoker print_sep((SepPrinter(oss)));
    BOOST_FOREACH(const StringSet& unique_key, unique_key_set) {
        print_sep();
        oss << "ADD ";
        PrintUniqueKey(oss, unique_key);
    }
    BOOST_FOREACH(const ForeignKey& foreign_key, foreign_key_set) {
        print_sep();
        oss << "ADD ";
        PrintForeignKey(oss, meta, foreign_key);
    }
    BOOST_FOREACH(const string& check, checks) {
        print_sep();
        oss << "ADD ";
        PrintCheck(oss, translator, check);
    }
    try {
        pqxx::subtransaction(work).exec(oss.str());
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


void RelVar::DropAllConstrs(pqxx::work& work)
{
    if (rich_header_.empty())
        return;
    ostringstream oss;
    oss << "SELECT ku.drop_all_constrs('" << name_ << "'); "
        << "ALTER TABLE " << Quoted(name_) << " ADD UNIQUE (";
    StringSet unique_key;
    OmitInvoker print_sep((SepPrinter(oss)));
    BOOST_FOREACH(const RichAttr& rich_attr, rich_header_) {
        string attr_name(rich_attr.GetName());
        unique_key.add_sure(attr_name);
        print_sep();
        oss << Quoted(attr_name);
    }
    oss << ')';
    try {
        pqxx::subtransaction(work).exec(oss.str());
    } catch (const pqxx::sql_error& err) {
        throw (string(err.what()).substr(0, 13) == "ERROR:  index"
               ? Error(Error::DB_QUOTA, "Unique string is too long")
               : Error(Error::REL_VAR_DEPENDENCY,
                       ("Unique cannot be dropped "
                        "because other RelVar references it")));
    }
    unique_key_set_.clear();
    unique_key_set_.add_sure(unique_key);
    foreign_key_set_.clear();
}

////////////////////////////////////////////////////////////////////////////////
// DBMeta definitions
////////////////////////////////////////////////////////////////////////////////

DBMeta::DBMeta(pqxx::work& work, const string& schema_name)
{
    static const format query("SELECT * FROM ku.get_schema_tables('%1%');");
    pqxx::result pqxx_result = work.exec((format(query) % schema_name).str());
    rel_vars_.reserve(pqxx_result.size());
    BOOST_FOREACH(const pqxx::result::tuple& tuple, pqxx_result) {
        KU_ASSERT_EQUAL(tuple.size(), 1U);
        KU_ASSERT(!tuple[0].is_null());
        rel_vars_.push_back(RelVar(work, tuple[0].c_str()));
    }
    BOOST_FOREACH(RelVar& rel_var, rel_vars_)
        rel_var.LoadConstrs(work, *this);
}


const RelVars& DBMeta::GetAll() const
{
    return rel_vars_;
}


const RelVar& DBMeta::Get(const string& rel_var_name) const
{
    return rel_vars_[GetIdxChecked(rel_var_name)];
}


RelVar& DBMeta::Get(const string& rel_var_name)
{
    return rel_vars_[GetIdxChecked(rel_var_name)];
}


void DBMeta::Create(pqxx::work& work,
                    const Translator& translator,
                    const string& rel_var_name,
                    const RichHeader& rich_header,
                    const UniqueKeySet& unique_key_set,
                    const ForeignKeySet& foreign_key_set,
                    const Strings& checks)
{
    if (rel_vars_.size() >= MAX_REL_VAR_NUMBER) {
        static const string message(
            (format("Maximum RelVar number is %1%") %
             MAX_REL_VAR_NUMBER).str());
        throw Error(Error::DB_QUOTA, message);
    }
    if (GetIdx(rel_var_name) != MINUS_ONE)
        throw Error(Error::REL_VAR_EXISTS,
                    "RelVar \"" + rel_var_name + "\" already exists");
    rel_vars_.push_back(RelVar(work,
                               translator,
                               *this,
                               rel_var_name,
                               rich_header,
                               unique_key_set,
                               foreign_key_set,
                               checks));
}


void DBMeta::Drop(pqxx::work& work, const StringSet& rel_var_names)
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
    OmitInvoker print_sep((SepPrinter(oss)));
    BOOST_FOREACH(const string& rel_var_name, rel_var_names) {
        print_sep();
        oss << Quoted(rel_var_name);
    }
    oss << " CASCADE";
    work.exec(oss.str());
    sort(indexes.begin(), indexes.end());
    BOOST_REVERSE_FOREACH(size_t index, indexes)
        rel_vars_.erase(rel_vars_.begin() + index);
}


size_t DBMeta::GetIdx(const string& rel_var_name) const
{
    for (size_t i = 0; i < rel_vars_.size(); ++i)
        if (rel_vars_[i].GetName() == rel_var_name)
            return i;
    return -1;
}


size_t DBMeta::GetIdxChecked(const string& rel_var_name) const
{
    size_t idx = GetIdx(rel_var_name);
    if (idx == MINUS_ONE)
        throw Error(Error::NO_SUCH_REL_VAR,
                    "No such RelVar: \"" + rel_var_name + '"');
    return idx;
}

////////////////////////////////////////////////////////////////////////////////
// Manager
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class Manager {
    public:
        Manager(const string& schema_name);
        void LoadMeta(pqxx::work& work);
        void CommitHappened();
        const DBMeta& GetMeta() const;
        DBMeta& ChangeMeta();
    
    private:
        boost::scoped_ptr<DBMeta> meta_;
        string schema_name_;
        bool consistent_;
    };
}

Manager::Manager(const string& schema_name)
    : schema_name_(schema_name)
    , consistent_(false)
{
}


void Manager::LoadMeta(pqxx::work& work)
{
    if (!consistent_) {
        meta_.reset(new DBMeta(work, schema_name_));
        consistent_ = true;
    }
}


void Manager::CommitHappened()
{
    consistent_ = true;
}


const DBMeta& Manager::GetMeta() const
{
    return *meta_;
}


DBMeta& Manager::ChangeMeta()
{
    consistent_ = false;
    return *meta_;
}

////////////////////////////////////////////////////////////////////////////////
// DBViewerImpl
////////////////////////////////////////////////////////////////////////////////

namespace
{
    // Implementation of DBViewer providing access to database for translator
    class DBViewerImpl : public DBViewer {
    public:
        DBViewerImpl(const Manager& manager, const Quoter& quoter);
        virtual const Header& GetHeader(const string& rel_var_name) const;
        virtual string Quote(const PgLiter& pg_liter) const;
        virtual RelVarAttrs GetReference(const RelVarAttrs& key) const;
        
    private:
        const Manager& manager_;
        Quoter quoter_;

        Error MakeKeyError(const RelVarAttrs& key, const string& message) const;
    };
}


DBViewerImpl::DBViewerImpl(const Manager& manager, const Quoter& quoter)
    : manager_(manager), quoter_(quoter)
{
}


const Header& DBViewerImpl::GetHeader(const string& rel_var_name) const
{
    return manager_.GetMeta().Get(rel_var_name).GetHeader();
}


string DBViewerImpl::Quote(const PgLiter& pg_liter) const
{
    return quoter_(pg_liter);
}


DBViewer::RelVarAttrs DBViewerImpl::GetReference(const RelVarAttrs& key) const
{
    const RelVar& rel_var(manager_.GetMeta().Get(key.rel_var_name));
    const ForeignKey* foreign_key_ptr = 0;
    BOOST_FOREACH(const ForeignKey& foreign_key, rel_var.GetForeignKeySet()) {
        if (foreign_key.key_attr_names == key.attr_names) {
            if (foreign_key_ptr)
                throw MakeKeyError(key, "has multiple keys on attributes");
            else
                foreign_key_ptr = &foreign_key;
        }
    }
    if (foreign_key_ptr)
        return RelVarAttrs(foreign_key_ptr->ref_rel_var_name,
                           foreign_key_ptr->ref_attr_names);
    else
        throw MakeKeyError(key, "doesn't have a key with attributes");
}


Error DBViewerImpl::MakeKeyError(const RelVarAttrs& key,
                                 const string& message) const
{
    ostringstream oss;
    oss << "RelVar \"" << key.rel_var_name << "\" " << message << ' ';
    OmitInvoker print_sep((SepPrinter(oss)));
    BOOST_FOREACH(const string& attr_name, key.attr_names) {
        print_sep();
        oss << attr_name;
    }
    return Error(Error::QUERY, oss.str());
}

////////////////////////////////////////////////////////////////////////////////
// QuotaChecker
////////////////////////////////////////////////////////////////////////////////

namespace
{
    // Database space quota checker. Full of heuristics.
    class QuotaChecker {
    public:
        QuotaChecker(const string& schema_name, uint64_t quota);
        void DataWereAdded(size_t rows_count, uint64_t size);
        void Check(pqxx::work& work);

    private:
        const string schema_name_;
        uint64_t quota_;
        uint64_t total_size_;
        uint64_t added_size_;
        size_t changed_rows_count_;

        uint64_t CalculateTotalSize(pqxx::work& work) const;
    };
}


QuotaChecker::QuotaChecker(const string& schema_name, uint64_t quota)
    : schema_name_(schema_name)
    , quota_(quota)
    , total_size_(quota)
    , added_size_(0)
    , changed_rows_count_(0)
{
}


void QuotaChecker::DataWereAdded(size_t rows_count, uint64_t size)
{
    changed_rows_count_ += rows_count;
    added_size_ += size;
}


void QuotaChecker::Check(pqxx::work& work)
{
    if (changed_rows_count_ > CHANGED_ROWS_COUNT_LIMIT ||
        (total_size_ + ADDED_SIZE_MULTIPLICATOR * added_size_ >= quota_)) {
        total_size_ = CalculateTotalSize(work);
        added_size_ = 0;
        changed_rows_count_ = 0;
    }
    if (total_size_ >= quota_)
        throw Error(Error::DB_QUOTA,
                    ("Database size quota exceeded, "
                     "updates and inserts are forbidden"));
}


uint64_t QuotaChecker::CalculateTotalSize(pqxx::work& work) const
{
    static const format query("SELECT ku.get_schema_size('%1%');");
    pqxx::result pqxx_result(work.exec((format(query) % schema_name_).str()));
    KU_ASSERT_EQUAL(pqxx_result.size(), 1U);
    KU_ASSERT_EQUAL(pqxx_result[0].size(), 1U);
    KU_ASSERT(!pqxx_result[0][0].is_null());
    return lexical_cast<uint64_t>(pqxx_result[0][0].c_str());
}

////////////////////////////////////////////////////////////////////////////////
// DB::Impl
////////////////////////////////////////////////////////////////////////////////

class DB::Impl {
public:
    Impl(const string& opt,
         const string& schema_name,
         const string& app_name,
         int try_count = 3);

    uint64_t GetDBQuota() const;
    uint64_t GetFSQuota() const;

    pqxx::connection& GetConnection();
    Manager& GetManager();
    const Translator& GetTranslator() const;
    QuotaChecker& GetQuotaChecker();

private:
    pqxx::connection conn_;
    int try_count_;
    uint64_t db_quota_;
    uint64_t fs_quota_;
    Manager manager_;
    DBViewerImpl db_viewer_;
    Translator translator_;
    scoped_ptr<QuotaChecker> quota_checker_ptr_;
};


DB::Impl::Impl(const string& opt,
               const string& schema_name,
               const string& app_name,
               int try_count)
    : conn_(opt)
    , try_count_(try_count)
    , manager_(schema_name)
    , db_viewer_(manager_, Quoter(conn_))
    , translator_(db_viewer_)
{
    static const format create_cmd("SELECT ku.create_schema('%1%');");
    static const format set_cmd("SET search_path TO \"%1%\", pg_catalog;");
    static const format quota_query("SELECT * FROM ku.get_app_quotas('%1%');");
    conn_.set_noticer(auto_ptr<pqxx::noticer>(new pqxx::nonnoticer()));
    {
        pqxx::work work(conn_);
        try {
            pqxx::subtransaction(work).exec(
                (format(create_cmd) % schema_name).str());
        } catch (const pqxx::sql_error&) {}
        work.exec((format(set_cmd) % schema_name).str());
        pqxx::result pqxx_result(
            work.exec((format(quota_query) % app_name).str()));
        KU_ASSERT_EQUAL(pqxx_result.size(), 1U);
        const pqxx::result::tuple& tuple(pqxx_result[0]);
        if (tuple[0].is_null())
            Fail("App \"" + app_name + "\" does not exist");
        db_quota_ = QUOTA_MULTIPLICATOR * tuple[0].as<size_t>();
        fs_quota_ = QUOTA_MULTIPLICATOR * tuple[1].as<size_t>();
        work.commit(); // don't remove it, stupid idiot! is sets search_path!
    }
    quota_checker_ptr_.reset(new QuotaChecker(schema_name, db_quota_));
}


uint64_t DB::Impl::GetDBQuota() const
{
    return db_quota_;
}


uint64_t DB::Impl::GetFSQuota() const
{
    return fs_quota_;
}


pqxx::connection& DB::Impl::GetConnection()
{
    return conn_;
}


Manager& DB::Impl::GetManager()
{
    return manager_;
}


const Translator& DB::Impl::GetTranslator() const
{
    return translator_;
}


QuotaChecker& DB::Impl::GetQuotaChecker()
{
    return *quota_checker_ptr_;
}

////////////////////////////////////////////////////////////////////////////////
// DB definitions
////////////////////////////////////////////////////////////////////////////////

DB::DB(const string& opt, const string& schema_name, const string& app_name)
{
    pimpl_.reset(new Impl(opt, schema_name, app_name));
}


DB::~DB()
{
}


uint64_t DB::GetDBQuota() const
{
    return pimpl_->GetDBQuota();
}


uint64_t DB::GetFSQuota() const
{
    return pimpl_->GetFSQuota();
}

////////////////////////////////////////////////////////////////////////////////
// Access definitions
////////////////////////////////////////////////////////////////////////////////

namespace
{
    Values GetTupleValues(const pqxx::result::tuple& tuple,
                          const Header& header)
    {
        KU_ASSERT_EQUAL(tuple.size(), header.size());
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


class Access::WorkWrapper {
public:
    explicit WorkWrapper(pqxx::connection& conn) : work_(conn) {}
    operator pqxx::work&()                 { return work_;             }
    void commit()                          { work_.commit();           }
    pqxx::result exec(const string& query) { return work_.exec(query); }
    string quote(const string& str) const  { return work_.quote(str);  }

private:
    pqxx::work work_;
};


Access::Access(DB& db)
    : db_impl_(*db.pimpl_)
    , work_ptr_(new WorkWrapper(db_impl_.GetConnection()))
{
    db_impl_.GetManager().LoadMeta(*work_ptr_);
}


Access::~Access()
{
}


void Access::Commit()
{
    work_ptr_->commit();
    db_impl_.GetManager().CommitHappened();
}


StringSet Access::GetNames() const
{
    const RelVars& rel_vars(db_impl_.GetManager().GetMeta().GetAll());
    StringSet result;
    result.reserve(rel_vars.size());
    BOOST_FOREACH(const RelVar& rel_var, rel_vars)
        result.add_sure(rel_var.GetName());
    return result;
}


const RichHeader& Access::GetRichHeader(const string& rel_var_name) const
{
    return db_impl_.GetManager().GetMeta().Get(rel_var_name).GetRichHeader();
}


const UniqueKeySet& Access::GetUniqueKeySet(const string& rel_var_name) const
{
    return db_impl_.GetManager().GetMeta().Get(rel_var_name).GetUniqueKeySet();
}


const ForeignKeySet& Access::GetForeignKeySet(const string& rel_var_name) const
{
    return db_impl_.GetManager().GetMeta().Get(rel_var_name).GetForeignKeySet();
}


void Access::Create(const string& name,
                    const RichHeader& rich_header,
                    const UniqueKeySet& unique_key_set,
                    const ForeignKeySet& foreign_key_set,
                    const Strings& checks)
{
    db_impl_.GetManager().ChangeMeta().Create(*work_ptr_,
                                              db_impl_.GetTranslator(),
                                              name,
                                              rich_header,
                                              unique_key_set,
                                              foreign_key_set,
                                              checks);
}


void Access::Drop(const StringSet& rel_var_names)
{
    db_impl_.GetManager().ChangeMeta().Drop(*work_ptr_, rel_var_names);
}


QueryResult Access::Query(const string& query,
                          const Drafts& query_params,
                          const Strings& by_exprs,
                          const Drafts& by_params,
                          size_t start,
                          size_t length) const
{
    Header header;
    string sql(db_impl_.GetTranslator().TranslateQuery(header,
                                                       query,
                                                       query_params,
                                                       by_exprs,
                                                       by_params,
                                                       start,
                                                       length));
    pqxx::result pqxx_result(work_ptr_->exec(sql));
    QueryResult result(header, vector<Values>());
    if (header.empty()) {
        if (!pqxx_result.empty())
            result.tuples.push_back(Values());
    } else {
        result.tuples.reserve(pqxx_result.size());
        BOOST_FOREACH(const pqxx::result::tuple& pqxx_tuple, pqxx_result)
            result.tuples.push_back(GetTupleValues(pqxx_tuple, result.header));
    }
    return result;
}


size_t Access::Count(const string& query, const Drafts& params) const
{
    string sql(db_impl_.GetTranslator().TranslateCount(query, params));
    pqxx::result pqxx_result(work_ptr_->exec(sql));
    KU_ASSERT_EQUAL(pqxx_result.size(), 1U);
    KU_ASSERT_EQUAL(pqxx_result[0].size(), 1U);
    return pqxx_result[0][0].as<size_t>();
}


size_t Access::Update(const string& rel_var_name,
                      const string& where,
                      const Drafts& where_params,
                      const StringMap& expr_map,
                      const Drafts& expr_params)
{
    db_impl_.GetQuotaChecker().Check(*work_ptr_);
    
    const RichHeader& rich_header(GetRichHeader(rel_var_name));
    uint64_t size = 0;
    BOOST_FOREACH(const StringMap::value_type& named_expr, expr_map) {
        rich_header.find(named_expr.first);
        // FIXME it's wrong estimation for expressions
        size += named_expr.second.size();
    }
    string sql(db_impl_.GetTranslator().TranslateUpdate(rel_var_name,
                                                        where,
                                                        where_params,
                                                        expr_map,
                                                        expr_params));
    size_t result;
    try {
        result = pqxx::subtransaction(*work_ptr_).exec(sql).affected_rows();
    } catch (const pqxx::integrity_constraint_violation& err) {
        throw Error(Error::CONSTRAINT, err.what());
    } catch (const pqxx::data_exception& err) {
        throw Error(Error::CONSTRAINT, err.what());
    } catch (const pqxx::sql_error& err) {
        throw Error(Error::DB, err.what());
    }
    db_impl_.GetQuotaChecker().DataWereAdded(result, size);
    return result;
}


size_t Access::Delete(const string& rel_var_name,
                      const string& where,
                      const Drafts& params)
{
    string sql(db_impl_.GetTranslator().TranslateDelete(rel_var_name,
                                                        where,
                                                        params));
    try {
        return pqxx::subtransaction(*work_ptr_).exec(sql).affected_rows();
    } catch (const pqxx::integrity_constraint_violation& err) {
        throw Error(Error::CONSTRAINT, err.what());
    } catch (const pqxx::sql_error& err) {
        throw Error(Error::DB, err.what());
    }
}


Values Access::Insert(const string& rel_var_name, const DraftMap& draft_map)
{
    static const format empty_cmd("SELECT ku.insert_into_empty('%1%');");
    static const format cmd(
        "INSERT INTO \"%1%\" (%2%) VALUES (%3%) RETURNING *;");
    static const format default_cmd(
        "INSERT INTO \"%1%\" DEFAULT VALUES RETURNING *;");

    db_impl_.GetQuotaChecker().Check(*work_ptr_);

    const RelVar& rel_var(db_impl_.GetManager().GetMeta().Get(rel_var_name));
    const RichHeader& rich_header(rel_var.GetRichHeader());
    string sql_str;
    uint64_t size = 0;
    if (rich_header.empty()) {
        if (!draft_map.empty())
            rich_header.find(draft_map.begin()->first); // throws
        sql_str = (format(empty_cmd) % rel_var_name).str();
    } else {
        if (!draft_map.empty()) {
            BOOST_FOREACH(const DraftMap::value_type& named_draft, draft_map)
                rich_header.find(named_draft.first);
            ostringstream names_oss, values_oss;
            OmitInvoker print_names_sep((SepPrinter(names_oss)));
            OmitInvoker print_values_sep((SepPrinter(values_oss)));
            Quoter quoter(db_impl_.GetConnection());
            BOOST_FOREACH(const RichAttr& rich_attr, rich_header) {
                DraftMap::const_iterator itr(
                    draft_map.find(rich_attr.GetName()));
                if (itr == draft_map.end()) {
                    if (rich_attr.GetTrait() != Type::SERIAL &&
                        !rich_attr.GetDefaultPtr())
                        throw Error(Error::ATTR_VALUE_REQUIRED,
                                    ("Value of attribute \"" +
                                     rich_attr.GetName() +
                                     "\" must be supplied"));
                } else {
                    print_names_sep();
                    names_oss << Quoted(rich_attr.GetName());
                    print_values_sep();
                    Value value(itr->second.Get(rich_attr.GetType()));
                    values_oss << quoter(value.GetPgLiter());
                }
            }
            sql_str = (format(cmd)
                       % rel_var_name
                       % names_oss.str()
                       % values_oss.str()).str();
            size += values_oss.str().size();
        } else {
            sql_str = (format(default_cmd) % rel_var_name).str();
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
        pqxx_result = pqxx::subtransaction(*work_ptr_).exec(sql_str);
    } catch (const pqxx::integrity_constraint_violation& err) {
        throw Error(Error::CONSTRAINT, err.what());
    } catch (const pqxx::data_exception& err) {
        throw Error(Error::CONSTRAINT, err.what());
    } catch (const pqxx::plpgsql_raise& err) {
        throw Error(Error::CONSTRAINT, err.what());
    } catch (const pqxx::sql_error& err) {
        throw Error(Error::DB, err.what());
    }
    db_impl_.GetQuotaChecker().DataWereAdded(1, size);
    if (rich_header.empty())
        return Values();
    KU_ASSERT_EQUAL(pqxx_result.size(), 1U);
    return GetTupleValues(pqxx_result[0], rel_var.GetHeader());
}


void Access::AddAttrs(const string& rel_var_name, const RichHeader& rich_attrs)
{
    RelVar& rel_var(db_impl_.GetManager().ChangeMeta().Get(rel_var_name));
    rel_var.AddAttrs(*work_ptr_, rich_attrs);
}


void Access::DropAttrs(const string& rel_var_name,
                       const StringSet& attr_names)
{
    RelVar& rel_var(db_impl_.GetManager().ChangeMeta().Get(rel_var_name));
    rel_var.DropAttrs(*work_ptr_, attr_names);
}


void Access::AddDefault(const string& rel_var_name, const DraftMap& draft_map)
{
    RelVar& rel_var(db_impl_.GetManager().ChangeMeta().Get(rel_var_name));
    rel_var.AddDefault(*work_ptr_, draft_map);
}


void Access::DropDefault(const string& rel_var_name,
                         const StringSet& attr_names)
{
    RelVar& rel_var(db_impl_.GetManager().ChangeMeta().Get(rel_var_name));
    rel_var.DropDefault(*work_ptr_, attr_names);
}


void Access::AddConstrs(const string& rel_var_name,
                        const UniqueKeySet& unique_key_set,
                        const ForeignKeySet& foreign_key_set,
                        const Strings& checks)
{
    DBMeta& meta(db_impl_.GetManager().ChangeMeta());
    RelVar& rel_var(meta.Get(rel_var_name));
    rel_var.AddConstrs(*work_ptr_,
                       db_impl_.GetTranslator(),
                       meta,
                       unique_key_set,
                       foreign_key_set,
                       checks);
}


void Access::DropAllConstrs(const std::string& rel_var_name)
{
    RelVar& rel_var(db_impl_.GetManager().ChangeMeta().Get(rel_var_name));
    rel_var.DropAllConstrs(*work_ptr_);
}


namespace
{
    Error NoSuchApp(const string& name)
    {
        return Error(Error::NO_SUCH_APP, "No such app: \"" + name + '"');
    }

    
    Strings StringsFromQueryResult(const pqxx::result& pqxx_result)
    {
        Strings result;
        result.reserve(pqxx_result.size());
        BOOST_FOREACH(const pqxx::result::tuple& tuple, pqxx_result) {
            KU_ASSERT_EQUAL(tuple.size(), 1U);
            result.push_back(tuple[0].as<string>());
        }
        return result;
    }
}


string Access::GetAppPatsakVersion(const string& name) const
{
    static const format query("SELECT ku.get_app_patsak_version(%1%);");
    pqxx::result pqxx_result(
        work_ptr_->exec((format(query) % work_ptr_->quote(name)).str()));
    KU_ASSERT_EQUAL(pqxx_result.size(), 1U);
    KU_ASSERT_EQUAL(pqxx_result[0].size(), 1U);
    if (pqxx_result[0][0].is_null())
        throw NoSuchApp(name);
    return pqxx_result[0][0].as<string>();
}


void Access::CheckAppExists(const string& name) const
{
    GetAppPatsakVersion(name);
}


App Access::DescribeApp(const string& name) const
{
    static const format app_query("SELECT * FROM ku.describe_app(%1%);");
    static const format devs_query("SELECT * FROM ku.get_app_devs(%1%);");
    static const format labels_query("SELECT * FROM ku.get_app_labels(%1%);");
    pqxx::result app_pqxx_result =
        work_ptr_->exec((format(app_query) % work_ptr_->quote(name)).str());
    KU_ASSERT(app_pqxx_result.size() < 2);
    if (!app_pqxx_result.size())
        throw NoSuchApp(name);
    const pqxx::result::tuple& app_tuple(app_pqxx_result[0]);
    unsigned app_id = app_tuple[0].as<unsigned>();
    pqxx::result devs_pqxx_result =
        work_ptr_->exec((format(devs_query) % app_id).str());
    pqxx::result labels_pqxx_result =
        work_ptr_->exec((format(labels_query) % app_id).str());
    return App(app_tuple[1].as<string>(),
               StringsFromQueryResult(devs_pqxx_result),
               app_tuple[2].as<string>(),
               app_tuple[3].as<string>(),
               StringsFromQueryResult(labels_pqxx_result));
}


string Access::GetUserEmail(const string& user_name) const
{
    static const format query("SELECT ku.get_user_email(%1%);");
    pqxx::result pqxx_result =
        work_ptr_->exec((format(query) % work_ptr_->quote(user_name)).str());
    KU_ASSERT_EQUAL(pqxx_result.size(), 1U);
    KU_ASSERT_EQUAL(pqxx_result[0].size(), 1U);
    if (pqxx_result[0][0].is_null())
        throw Error(Error::NO_SUCH_USER, "No such user: \"" + user_name + '"');
    return pqxx_result[0][0].as<string>();
}


void Access::CheckUserExists(const string& user_name) const
{
    GetUserEmail(user_name);
}


namespace
{
    Strings GetApps(const Access& access,
                    pqxx::work& work,
                    const format& query,
                    const string& user_name)
    {
        pqxx::result pqxx_result =
            work.exec((format(query) % work.quote(user_name)).str());
        if (pqxx_result.empty())
            access.CheckUserExists(user_name);
        return StringsFromQueryResult(pqxx_result);
    }
}


Strings Access::GetAdminedApps(const string& user_name) const
{
    static const format query("SELECT * FROM ku.get_admined_apps(%1%);");
    return GetApps(*this, *work_ptr_, query, user_name);
}


Strings Access::GetDevelopedApps(const string& user_name) const
{
    static const format query("SELECT * FROM ku.get_developed_apps(%1%);");
    return GetApps(*this, *work_ptr_, query, user_name);
}


Strings Access::GetAppsByLabel(const string& label_name) const
{
    static const format query("SELECT * FROM ku.get_apps_by_label(%1%);");
    return StringsFromQueryResult(
        work_ptr_->exec((format(query) % work_ptr_->quote(label_name)).str()));
}
