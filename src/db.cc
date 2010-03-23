
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
    const size_t MAX_STRING_SIZE = 100 * 1024;
    const unsigned long long QUOTA_MULTIPLICATOR = 1024 * 1024;
    
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

        std::string operator()(const PgLiter& pg_liter) const {
            return pg_liter.quote_me ? conn_.quote(pg_liter.str) : pg_liter.str;
        }

        std::string operator()(const Value& value) const {
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
                   const Type& type,
                   Type::Trait trait,
                   const Value* default_ptr)
    : attr_(name, type)
    , trait_(trait)
    , default_ptr_(default_ptr ? new Value(default_ptr->Cast(GetType())) : 0)
{
    KU_ASSERT(GetType().IsApplicable(trait_));
    KU_ASSERT(!(trait_ == Type::SERIAL && default_ptr));
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

////////////////////////////////////////////////////////////////////////////////
// RelVar
///////////////////////////////////////////////////////////////////////////////

namespace
{
    class RelVar {
    public:
        RelVar(pqxx::work& work, const string& name);
        RelVar(const string& name,
               const RichHeader& rich_header,
               const Constrs& constrs);
        string GetName() const;
        const RichHeader& GetRichHeader() const;
        const Header& GetHeader() const;
        const Constrs& GetConstrs() const;
        Constrs& GetConstrs();

    private:
        string name_;
        RichHeader rich_header_;
        Header header_;
        Constrs constrs_;

        void InitHeader();
    };
    
    
    typedef vector<RelVar> RelVars;
}


RelVar::RelVar(pqxx::work& work, const string& name)
    : name_(name)
{
    static const format query("SELECT * FROM ku.describe_table('\"%1%\"');");
    pqxx::result pqxx_result = work.exec((format(query) % name_).str());
    rich_header_.reserve(pqxx_result.size());
    BOOST_FOREACH(const pqxx::result::tuple& tuple, pqxx_result) {
        KU_ASSERT(tuple.size() == 3);
        KU_ASSERT(!tuple[0].is_null() && ! tuple[1].is_null());
        string name(tuple[0].c_str());
        Type type(PgType(tuple[1].c_str()));
        Type::Trait trait = Type::COMMON;
        auto_ptr<Value> default_ptr;
        if (string(tuple[1].c_str()) == "int4") {
            KU_ASSERT(type == Type::NUMBER);
            trait = Type::INTEGER;
        }
        if (!tuple[2].is_null()) {
            string default_str(tuple[2].c_str());
            if (default_str.substr(0, 8) == "nextval(") {
                KU_ASSERT(type == Type::NUMBER && trait == Type::INTEGER);
                trait = Type::SERIAL;
            } else {
                default_ptr.reset(new Value(type, default_str));
            }
        }
        rich_header_.add_sure(RichAttr(name, type, trait, default_ptr.get()));
    }
    InitHeader();
}


RelVar::RelVar(const string& name,
               const RichHeader& rich_header,
               const Constrs& constrs)
    : name_(name)
    , rich_header_(rich_header)
    , constrs_(constrs)
{
    InitHeader();
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


const Constrs& RelVar::GetConstrs() const
{
    return constrs_;
}


Constrs& RelVar::GetConstrs()
{
    return constrs_;
}


void RelVar::InitHeader()
{
    header_.reserve(rich_header_.size());
    BOOST_FOREACH(const RichAttr& rich_attr, rich_header_)
        header_.add_sure(rich_attr.GetAttr());
}

////////////////////////////////////////////////////////////////////////////////
// DBMeta and its functors declarations
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class DBMeta {
    public:
        DBMeta(pqxx::work& work, const string& schema_name);
        const RelVar& GetRelVar(const string& rel_var_name) const;
        const RelVars& GetRelVars() const;
        
        void CreateRelVar(pqxx::work& work,
                          const Translator& translator,
                          const string& rel_var_name,
                          const RichHeader& rich_header,
                          const Constrs& constrs);
        
        void DropRelVars(pqxx::work& work, const orset<string>& rel_var_names);

    private:
        RelVars rel_vars_;

        size_t GetRelVarIdx(const string& rel_var_name) const; // never throws
        static void CheckName(const string& name);
    };

    
    class ConstrsLoader {
    public:
        ConstrsLoader(RelVars& rel_vars);
        void operator()(pqxx::work& work) const;

    private:
        RelVars& rel_vars_;

        void LoadConstrs(pqxx::work& work, RelVar& rel_var) const;
        
        void SetConstrByPgTuple(RelVar& rel_var,
                                const pqxx::result::tuple& tuple) const;
        
        static StringSet GetFieldsByPgArray(const RichHeader& rich_header,
                                            const string& pg_array);
        
        static vector<size_t> ReadPgArray(const string& pg_array);
        const RelVar& GetRelVarByName(const string& rel_var_name) const;
    };
    

    class RelVarCreator {
    public:
        RelVarCreator(const DBMeta& db_meta, const RelVar& rel_var);

        void operator()(pqxx::work& work, const Translator& translator) const;

    private:
        const RelVar& rel_var_;

        void PrintHeader(ostream& os,
                         OmitInvoker& print_sep,
                         const Quoter& quoter) const;

        void PrintConstrs(const Translator& translator,
                          ostream& os,
                          OmitInvoker& print_sep) const;

        void PrintCreateSequences(ostream& os) const;
        void PrintAlterSequences(ostream& os) const;
    };
    

    // RelVar drop functor. Works on a group. Requires it not to have
    // foreign key reference relations with dependents outside the group.
    class RelVarsDropper {
    public:
        RelVarsDropper(RelVars& rel_vars,
                       const orset<string>& del_names);
        void operator()(pqxx::work& work) const;

    private:
        RelVars& rel_vars_;
        orset<string> del_names_;

        vector<size_t> Prepare() const;
        size_t GetDelNameIdx(const string& rel_var_name) const;
    };    
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
        KU_ASSERT(tuple.size() == 1);
        KU_ASSERT(!tuple[0].is_null());
        rel_vars_.push_back(RelVar(work, tuple[0].c_str()));
    }
    ConstrsLoader constrs_loader(rel_vars_);
    constrs_loader(work);
}

const RelVars& DBMeta::GetRelVars() const
{
    return rel_vars_;
}


const RelVar& DBMeta::GetRelVar(const string& rel_var_name) const
{
    size_t idx = GetRelVarIdx(rel_var_name);
    if (idx == MINUS_ONE)
        throw Error(Error::NO_SUCH_REL_VAR,
                    "No such RelVar: \"" + rel_var_name + '"');
    return rel_vars_[idx];
}


void DBMeta::CreateRelVar(pqxx::work& work,
                          const Translator& translator,
                          const string& rel_var_name,
                          const RichHeader& rich_header,
                          const Constrs& constrs)
{
    if (rel_vars_.size() >= MAX_REL_VAR_NUMBER) {
        static const string message(
            (format("Maximum RelVar number is %1%") %
             MAX_REL_VAR_NUMBER).str());
        throw Error(Error::DB_QUOTA, message);
    }
    CheckName(rel_var_name);
    if (rich_header.size() > MAX_ATTR_NUMBER) {
        static const string message(
            (format("Maximum attribute number is %1%") %
             MAX_ATTR_NUMBER).str());
        throw Error(Error::DB_QUOTA, message);
    }
    if (GetRelVarIdx(rel_var_name) != MINUS_ONE)
        throw Error(Error::REL_VAR_EXISTS,
                    "RelVar \"" + rel_var_name + "\" already exists");
    BOOST_FOREACH(const RichAttr& rich_attr, rich_header)
        CheckName(rich_attr.GetName());
    RelVar rel_var(rel_var_name, rich_header, constrs);
    if (!rich_header.empty()) {
        StringSet all_field_names;
        all_field_names.reserve(rich_header.size());
        BOOST_FOREACH(const RichAttr& rich_attr, rich_header)
            all_field_names.add_sure(rich_attr.GetName());
        rel_var.GetConstrs().push_back(Unique(all_field_names));
    }
    RelVarCreator(*this, rel_var)(work, translator);
    rel_vars_.push_back(rel_var);
}


void DBMeta::DropRelVars(pqxx::work& work, const orset<string>& rel_var_names)
{
    RelVarsDropper(rel_vars_, rel_var_names)(work);
}


size_t DBMeta::GetRelVarIdx(const string& rel_var_name) const
{
    for (size_t i = 0; i < rel_vars_.size(); ++i)
        if (rel_vars_[i].GetName() == rel_var_name)
            return i;
    return -1;
}


void DBMeta::CheckName(const string& name)
{
    if (name.empty())
        throw Error(Error::USAGE, "Identifier can't be empty");
    if (name.size() > MAX_NAME_SIZE) {
        static const string message(
            (format("RelVar and attribute name length must be "
                    "no more than %1% characters") %
             MAX_NAME_SIZE).str());
        throw Error(Error::DB_QUOTA, message);
    }
    const locale& loc(locale::classic());
    if (name[0] != '_' && !isalpha(name[0], loc))
        throw Error(Error::USAGE,
                    ("First identifier character must be "
                     "a letter or underscore"));
    for (size_t i = 1; i < name.size(); ++i)
        if (name[i] != '_' && !isalnum(name[i], loc))
            throw Error(Error::USAGE,
                        ("Identifier must consist only of "
                         "letters, digits or underscores"));
}

////////////////////////////////////////////////////////////////////////////////
// ConstrsLoader definitions
////////////////////////////////////////////////////////////////////////////////

ConstrsLoader::ConstrsLoader(RelVars& rel_vars)
    : rel_vars_(rel_vars) {}


void ConstrsLoader::operator()(pqxx::work& work) const
{
    BOOST_FOREACH(RelVar& rel_var, rel_vars_)
        LoadConstrs(work, rel_var);
}


void ConstrsLoader::LoadConstrs(pqxx::work& work, RelVar& rel_var) const
{
    static const format query("SELECT * FROM ku.describe_constrs('\"%1%\"')");
    pqxx::result pqxx_result =
        work.exec((format(query) % rel_var.GetName()).str());
    rel_var.GetConstrs().reserve(pqxx_result.size());
    BOOST_FOREACH(const pqxx::result::tuple& tuple, pqxx_result)
        SetConstrByPgTuple(rel_var, tuple);
}


void ConstrsLoader::SetConstrByPgTuple(RelVar& rel_var,
                                       const pqxx::result::tuple& tuple) const
{
    KU_ASSERT(tuple.size() == 4 && !tuple[0].is_null() && !tuple[1].is_null());
    KU_ASSERT(string(tuple[0].c_str()).size() == 1);
    
    Constrs& constrs(rel_var.GetConstrs());
    const RichHeader& rich_header(rel_var.GetRichHeader());
    StringSet field_names(GetFieldsByPgArray(rich_header, tuple[1].c_str()));
    char constr_code = tuple[0].c_str()[0];
    switch (constr_code) {
    case 'p':
    case 'u':
        constrs.push_back(Unique(field_names));
        break;
    case 'f': {
        KU_ASSERT(!tuple[2].is_null() && !tuple[3].is_null());
        const RichHeader&
            ref_rich_header(GetRelVarByName(tuple[2].c_str()).GetRichHeader());
        StringSet
            ref_field_names(GetFieldsByPgArray(ref_rich_header,
                                               tuple[3].c_str()));
        constrs.push_back(ForeignKey(field_names,
                                     tuple[2].c_str(),
                                     ref_field_names));
        break;
    }
    default:
        KU_ASSERT(constr_code == 'c');
        // TODO implement check constrs loading or at least loading of
        // their names
    };
}


StringSet ConstrsLoader::GetFieldsByPgArray(const RichHeader& rich_header,
                                            const string& pg_array)
{
    vector<size_t> indexes(ReadPgArray(pg_array));
    StringSet result;
    result.reserve(indexes.size());
    BOOST_FOREACH(size_t index, indexes) {
        KU_ASSERT(index > 0 && index <= rich_header.size());
        result.add_sure(rich_header[index - 1].GetName());
    }
    return result;
}


vector<size_t> ConstrsLoader::ReadPgArray(const string& pg_array)
{
    KU_ASSERT(pg_array.size() >= 2);
    istringstream iss(pg_array.substr(1, pg_array.size() - 2));
    vector<size_t> result;
    for (;;) {
        size_t item;
        iss >> item;
        KU_ASSERT(!iss.fail());
        result.push_back(item);
        if (iss.eof())
            break;
        char comma;
        iss.get(comma);
        KU_ASSERT(comma == ',');
    }
    return result;
}


const RelVar& ConstrsLoader::GetRelVarByName(const string& rel_var_name) const
{
    BOOST_FOREACH(const RelVar& rel_var, rel_vars_)
        if (rel_var.GetName() == rel_var_name)
            return rel_var;
    Fail("Constraint to unavaliable RelVar \"" + rel_var_name + '"');
}

////////////////////////////////////////////////////////////////////////////////
// RelVarCreator definitions
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class ConstrChecker : public static_visitor<void> {
    public:
        ConstrChecker(const DBMeta& db_meta,
                      const RelVar& rel_var)
            : db_meta_(db_meta), rel_var_(rel_var) {}

        void operator()(const Unique& unique) const {
            if (unique.field_names.empty())
                throw Error(Error::USAGE, "Empty unique field set");
            BOOST_FOREACH(const string& field_name, unique.field_names)
                rel_var_.GetRichHeader().find(field_name);
        }

        void operator()(const ForeignKey& foreign_key) const {
            if (foreign_key.key_field_names.empty())
                throw Error(Error::USAGE,
                            "Foreign key with empty key field set");
            if (foreign_key.key_field_names.size() !=
                foreign_key.ref_field_names.size())
                throw Error(Error::USAGE, "Ref-key fields size mismatch");

            const RichHeader& key_rich_header(rel_var_.GetRichHeader());
            const RelVar&
                ref_rel_var(foreign_key.ref_rel_var_name == rel_var_.GetName()
                            ? rel_var_
                            : db_meta_.GetRelVar(foreign_key.ref_rel_var_name));
            const RichHeader& ref_rich_header(ref_rel_var.GetRichHeader());
            
            for (size_t i = 0; i < foreign_key.key_field_names.size(); ++i) {
                string key_field_name = foreign_key.key_field_names[i];
                string ref_field_name = foreign_key.ref_field_names[i];
                RichAttr key_rich_attr(key_rich_header.find(key_field_name));
                RichAttr ref_rich_attr(ref_rich_header.find(ref_field_name));
                Type key_field_type(key_rich_attr.GetType());
                Type ref_field_type(ref_rich_attr.GetType());
                Type::Trait key_field_trait(key_rich_attr.GetTrait());
                Type::Trait ref_field_trait(ref_rich_attr.GetTrait());
                if (key_field_type != ref_field_type ||
                    !Type::TraitsAreCompatible(key_field_trait,
                                               ref_field_trait))
                    throw Error(Error::USAGE,
                                ("Foreign key field types mismatch: \"" +
                                 rel_var_.GetName() + '.' +
                                 key_field_name + "\" is " +
                                 key_field_type.GetKuStr(key_field_trait) +
                                 ", \"" +
                                 foreign_key.ref_rel_var_name + '.' +
                                 ref_field_name + "\" is " +
                                 ref_field_type.GetKuStr(ref_field_trait)));
            }
            
            BOOST_FOREACH(const Constr& constr, ref_rel_var.GetConstrs()) {
                const Unique* unique_ptr = boost::get<Unique>(&constr);
                if (unique_ptr &&
                    unique_ptr->field_names == foreign_key.ref_field_names)
                    return;
            }
            throw Error(Error::USAGE, "Foreign key ref fields must be unique");
        }

        void operator()(const Check& /*check*/) const {}

    private:
        const DBMeta& db_meta_;
        const RelVar& rel_var_;
    };
}


RelVarCreator::RelVarCreator(const DBMeta& db_meta, const RelVar& rel_var)
    : rel_var_(rel_var)
{
    BOOST_FOREACH(const Constr& constr, rel_var_.GetConstrs())
        apply_visitor(ConstrChecker(db_meta, rel_var_), constr);
}


void RelVarCreator::operator()(pqxx::work& work,
                               const Translator& translator) const
{
    ostringstream oss;
    PrintCreateSequences(oss);
    oss << "CREATE TABLE \"" << rel_var_.GetName() << "\" (";
    OmitInvoker print_sep((SepPrinter(oss)));
    PrintHeader(oss, print_sep, Quoter(work));
    PrintConstrs(translator, oss, print_sep);
    oss << ");";
    PrintAlterSequences(oss);
    work.exec(oss.str());
}


void RelVarCreator::PrintHeader(ostream& os,
                                OmitInvoker& print_sep,
                                const Quoter& quoter) const
{
    BOOST_FOREACH(const RichAttr& rich_attr, rel_var_.GetRichHeader()) {
        print_sep();
        os << Quoted(rich_attr.GetName()) << ' '
           << rich_attr.GetType().GetPgStr(rich_attr.GetTrait())
           << " NOT NULL";
        const Value* default_ptr(rich_attr.GetDefaultPtr());
        if (default_ptr)
            os << " DEFAULT " << quoter(default_ptr->GetPgLiter());
        else if (rich_attr.GetTrait() == Type::SERIAL)
            os << " DEFAULT nextval('\""
               << rel_var_.GetName() << '@' << rich_attr.GetName()
               << "\"')";
        if (rich_attr.GetType() == Type::STRING)
            os << " CHECK (bit_length("
               << Quoted(rich_attr.GetName())
               << ") <= " << 8 * MAX_STRING_SIZE
               << ')';
    }
}


namespace
{
    class ConstrPrinter : public static_visitor<void> {
    public:
        ConstrPrinter(const Translator& translator,
                      const RelVar& rel_var,
                      ostream& os,
                      OmitInvoker& print_sep)
            : translator_(translator)
            , rel_var_(rel_var)
            , os_(os)
            , print_sep_(print_sep) {}

        void operator()(const Unique& unique) {
            print_sep_();
            os_ << "UNIQUE ";
            PrintNameSet(unique.field_names);
        }
        
        void operator()(const ForeignKey& foreign_key) {
            print_sep_();
            os_ << "FOREIGN KEY ";
            PrintNameSet(foreign_key.key_field_names);
            os_ << " REFERENCES " << Quoted(foreign_key.ref_rel_var_name);
            PrintNameSet(foreign_key.ref_field_names);
        }
        
        void operator()(const Check& check) {
            print_sep_();
            os_ << "CHECK ("
                << translator_.TranslateExpr(check.expr_str,
                                             rel_var_.GetName(),
                                             rel_var_.GetHeader())
                << ')';
        }

    private:
        const Translator& translator_;
        const RelVar& rel_var_;
        ostream& os_;
        OmitInvoker& print_sep_;

        void PrintNameSet(const StringSet& name_set) {
            OmitInvoker print_local_sep((SepPrinter(os_)));
            os_ << '(';
            BOOST_FOREACH(const string& name, name_set) {
                print_local_sep();
                os_ << Quoted(name);
            }
            os_ << ')';
        }
    };
}


void RelVarCreator::PrintConstrs(const Translator& translator,
                                 ostream& os,
                                 OmitInvoker& print_sep) const
{
    ConstrPrinter constr_printer(translator, rel_var_, os, print_sep);
    BOOST_FOREACH(const Constr& constr, rel_var_.GetConstrs())
        apply_visitor(constr_printer, constr);
}


void RelVarCreator::PrintCreateSequences(ostream& os) const
{
    static const format cmd("CREATE SEQUENCE \"%1%@%2%\" MINVALUE 0 START 0;");
    BOOST_FOREACH(const RichAttr& rich_attr, rel_var_.GetRichHeader())
        if (rich_attr.GetTrait() == Type::SERIAL)
            os << (format(cmd) % rel_var_.GetName() % rich_attr.GetName());
}


void RelVarCreator::PrintAlterSequences(ostream& os) const
{
    static const format cmd(
        "ALTER SEQUENCE \"%1%@%2%\" OWNED BY \"%1%\".\"%2%\";");
    BOOST_FOREACH(const RichAttr& rich_attr, rel_var_.GetRichHeader())
        if (rich_attr.GetTrait() == Type::SERIAL)
            os << (format(cmd) % rel_var_.GetName() % rich_attr.GetName());
}

////////////////////////////////////////////////////////////////////////////////
// RelVarsDropper definitions
////////////////////////////////////////////////////////////////////////////////

RelVarsDropper::RelVarsDropper(RelVars& rel_vars,
                               const orset<string>& del_names)
    : rel_vars_(rel_vars)
    , del_names_(del_names)
{
}


void RelVarsDropper::operator()(pqxx::work& work) const
{
    if (del_names_.empty())
        return;
    vector<size_t> del_indexes(Prepare());
    ostringstream oss;
    OmitInvoker print_sep((SepPrinter(oss)));
    BOOST_FOREACH(const string& del_name, del_names_) {
        print_sep();
        oss << Quoted(del_name);
    }
    work.exec("DROP TABLE " + oss.str() + " CASCADE;");
    sort(del_indexes.begin(), del_indexes.end(), greater<size_t>());
    BOOST_FOREACH(size_t index, del_indexes)
        rel_vars_.erase(rel_vars_.begin() + index);
}


vector<size_t> RelVarsDropper::Prepare() const
{
    vector<size_t> result(del_names_.size(), -1);
    for (size_t i = 0; i < rel_vars_.size(); ++i) {
        const RelVar& rel_var(rel_vars_[i]);
        size_t idx = GetDelNameIdx(rel_var.GetName());
        if (idx != MINUS_ONE) {
            result[idx] = i;
            continue;
        }
        BOOST_FOREACH(const Constr& constr, rel_var.GetConstrs()) {
            const ForeignKey* foreign_key_ptr = boost::get<ForeignKey>(&constr);
            const string& ref_rel_var_name(foreign_key_ptr->ref_rel_var_name);
            if (foreign_key_ptr &&
                GetDelNameIdx(ref_rel_var_name) != MINUS_ONE)
                throw Error(Error::REL_VAR_DEPENDENCY,
                            ("Attempt to delete a group of RelVars "
                             "with a RelVar \"" +
                             ref_rel_var_name +
                             "\" but without a RelVar \"" +
                             rel_var.GetName() +
                             "\" it is dependent on"));
        }
    }
    vector<size_t>::const_iterator itr(find(result.begin(), result.end(), -1));
    if (itr != result.end())
        throw Error(Error::NO_SUCH_REL_VAR,
                    ("No such RelVar: \"" +
                     del_names_[itr - result.begin()] + '"'));
    return result;
}


size_t RelVarsDropper::GetDelNameIdx(const string& rel_var_name) const
{
    orset<string>::const_iterator itr(find(del_names_.begin(),
                                           del_names_.end(),
                                           rel_var_name));
    return (itr == del_names_.end()
            ? MINUS_ONE
            : itr - del_names_.begin());
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
        virtual const Header& GetRelVarHeader(const string& rel_var_name) const;
        virtual string Quote(const PgLiter& pg_liter) const;
        virtual RelVarFields GetReference(const RelVarFields& key) const;
        
    private:
        const Manager& manager_;
        Quoter quoter_;

        Error MakeKeyError(const RelVarFields& key,
                           const string& message) const;
    };
}


DBViewerImpl::DBViewerImpl(const Manager& manager, const Quoter& quoter)
    : manager_(manager), quoter_(quoter)
{
}


const Header& DBViewerImpl::GetRelVarHeader(const string& rel_var_name) const
{
    return manager_.GetMeta().GetRelVar(rel_var_name).GetHeader();
}


string DBViewerImpl::Quote(const PgLiter& pg_liter) const
{
    return quoter_(pg_liter);
}


DBViewer::RelVarFields DBViewerImpl::GetReference(const RelVarFields& key) const
{
    const RelVar& rel_var(manager_.GetMeta().GetRelVar(key.rel_var_name));
    const ForeignKey* found_foreign_key_ptr = 0;
    BOOST_FOREACH(const Constr& constr, rel_var.GetConstrs()) {
        const ForeignKey* foreign_key_ptr = boost::get<ForeignKey>(&constr);
        if (foreign_key_ptr &&
            foreign_key_ptr->key_field_names == key.field_names) {
            if (found_foreign_key_ptr)
                throw MakeKeyError(key, "has multiple keys on fields");
            else
                found_foreign_key_ptr = foreign_key_ptr;
        }
    }

    if (found_foreign_key_ptr)
        return RelVarFields(found_foreign_key_ptr->ref_rel_var_name,
                            found_foreign_key_ptr->ref_field_names);
    else
        throw MakeKeyError(key, "doesn't have a key with fields");
}


Error DBViewerImpl::MakeKeyError(const RelVarFields& key,
                                 const string& message) const
{
    ostringstream oss;
    oss << "RelVar \"" << key.rel_var_name << "\" " << message << ' ';
    OmitInvoker print_sep((SepPrinter(oss)));
    BOOST_FOREACH(const string& field, key.field_names) {
        print_sep();
        oss << field;
    }
    return Error(Error::QUERY, oss.str());
}

////////////////////////////////////////////////////////////////////////////////
// QuotaController
////////////////////////////////////////////////////////////////////////////////

namespace
{
    // Database space quota controller. Full of heuristics.
    class QuotaController {
    public:
        QuotaController(const string& schema_name, unsigned long long quota);
        void DataWereAdded(unsigned long rows_count, unsigned long long size);
        void Check(pqxx::work& work);

    private:
        const string schema_name_;
        unsigned long long quota_;
        unsigned long long total_size_;
        unsigned long long added_size_;
        unsigned long changed_rows_count_;

        unsigned long long CalculateTotalSize(pqxx::work& work) const;
    };
}


QuotaController::QuotaController(const string& schema_name,
                                 unsigned long long quota)
    : schema_name_(schema_name)
    , quota_(quota)
    , total_size_(quota)
    , added_size_(0)
    , changed_rows_count_(0)
{
}


void QuotaController::DataWereAdded(unsigned long rows_count,
                                    unsigned long long size)
{
    changed_rows_count_ += rows_count;
    added_size_ += size;
}


void QuotaController::Check(pqxx::work& work)
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


unsigned long long QuotaController::CalculateTotalSize(pqxx::work& work) const
{
    static const format query("SELECT ku.get_schema_size('%1%');");
    pqxx::result pqxx_result(work.exec((format(query) % schema_name_).str()));
    KU_ASSERT(pqxx_result.size() == 1 &&
              pqxx_result[0].size() == 1 &&
              !pqxx_result[0][0].is_null());
    return lexical_cast<unsigned long long>(pqxx_result[0][0].c_str());
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

    unsigned long long GetDBQuota() const;
    unsigned long long GetFSQuota() const;

    pqxx::connection& GetConnection();
    Manager& GetManager();
    const Translator& GetTranslator() const;
    QuotaController& GetQuotaController();

private:
    pqxx::connection conn_;
    int try_count_;
    unsigned long long db_quota_;
    unsigned long long fs_quota_;
    Manager manager_;
    DBViewerImpl db_viewer_;
    Translator translator_;
    scoped_ptr<QuotaController> quota_controller_ptr_;
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
        pqxx::subtransaction sub_work(work);
        try {
            sub_work.exec((format(create_cmd) % schema_name).str());
        } catch (const pqxx::sql_error&) {
            sub_work.abort();
        }
        work.exec((format(set_cmd) % schema_name).str());
        pqxx::result pqxx_result(
            work.exec((format(quota_query) % app_name).str()));
        KU_ASSERT(pqxx_result.size() == 1);
        const pqxx::result::tuple& tuple(pqxx_result[0]);
        if (tuple[0].is_null())
            Fail("App \"" + app_name + "\" does not exist");
        db_quota_ = QUOTA_MULTIPLICATOR * tuple[0].as<size_t>();
        fs_quota_ = QUOTA_MULTIPLICATOR * tuple[1].as<size_t>();
        work.commit(); // don't remove it, stupid idiot! is sets search_path!
    }
    quota_controller_ptr_.reset(new QuotaController(schema_name, db_quota_));
}


unsigned long long DB::Impl::GetDBQuota() const
{
    return db_quota_;
}


unsigned long long DB::Impl::GetFSQuota() const
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


QuotaController& DB::Impl::GetQuotaController()
{
    return *quota_controller_ptr_;
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


unsigned long long DB::GetDBQuota() const
{
    return pimpl_->GetDBQuota();
}


unsigned long long DB::GetFSQuota() const
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
        KU_ASSERT(tuple.size() == header.size());
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


StringSet Access::GetRelVarNames() const
{
    const RelVars& rel_vars(db_impl_.GetManager().GetMeta().GetRelVars());
    StringSet result;
    result.reserve(rel_vars.size());
    BOOST_FOREACH(const RelVar& rel_var, rel_vars)
        result.add_sure(rel_var.GetName());
    return result;
}


const RichHeader& Access::GetRelVarRichHeader(const string& rel_var_name) const
{
    return
        db_impl_.GetManager().GetMeta().GetRelVar(rel_var_name).GetRichHeader();
}


const Constrs& Access::GetRelVarConstrs(const string& rel_var_name) const
{
    return db_impl_.GetManager().GetMeta().GetRelVar(rel_var_name).GetConstrs();
}


void Access::CreateRelVar(const string& name,
                          const RichHeader& rich_header,
                          const Constrs& constrs)
{
    db_impl_.GetManager().ChangeMeta().CreateRelVar(
        *work_ptr_, db_impl_.GetTranslator(), name, rich_header, constrs);
}


void Access::DropRelVars(const StringSet& rel_var_names)
{
    db_impl_.GetManager().ChangeMeta().DropRelVars(*work_ptr_, rel_var_names);
}


QueryResult Access::Query(const string& query,
                          const Values& query_params,
                          const Strings& by_exprs,
                          const Values& by_params,
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


size_t Access::Count(const string& query, const Values& params) const
{
    string sql(db_impl_.GetTranslator().TranslateCount(query, params));
    pqxx::result pqxx_result(work_ptr_->exec(sql));
    KU_ASSERT(pqxx_result.size() == 1 && pqxx_result[0].size() == 1);
    return pqxx_result[0][0].as<size_t>();
}


size_t Access::Update(const string& rel_var_name,
                      const string& where,
                      const Values& where_params,
                      const StringMap& field_expr_map,
                      const Values& expr_params)
{
    db_impl_.GetQuotaController().Check(*work_ptr_);
    
    const RichHeader& rich_header(GetRelVarRichHeader(rel_var_name));
    unsigned long long size = 0;
    BOOST_FOREACH(const StringMap::value_type& field_expr, field_expr_map) {
        rich_header.find(field_expr.first);
        // FIXME it's wrong estimation for expressions
        size += field_expr.second.size();
    }
    string sql(db_impl_.GetTranslator().TranslateUpdate(rel_var_name,
                                                        where,
                                                        where_params,
                                                        field_expr_map,
                                                        expr_params));
    size_t result;
    pqxx::subtransaction sub_work(*work_ptr_);
    try {
        result = sub_work.exec(sql).affected_rows();
    } catch (const pqxx::integrity_constraint_violation& err) {
        sub_work.abort();
        throw Error(Error::CONSTRAINT, err.what());
    } catch (const pqxx::data_exception& err) {
        KU_ASSERT(string(err.what()) == "ERROR:  integer out of range\n");
        sub_work.abort();
        throw Error(Error::CONSTRAINT, err.what());
    }
    db_impl_.GetQuotaController().DataWereAdded(result, size);
    sub_work.commit();
    return result;
}


size_t Access::Delete(const string& rel_var_name,
                      const string& where,
                      const Values& params)
{
    string sql(db_impl_.GetTranslator().TranslateDelete(rel_var_name,
                                                        where,
                                                        params));
    size_t result;
    pqxx::subtransaction sub_work(*work_ptr_);
    try {
        result = sub_work.exec(sql).affected_rows();
    } catch (const pqxx::integrity_constraint_violation& err) {
        sub_work.abort();
        throw Error(Error::CONSTRAINT, err.what());
    }
    sub_work.commit();
    return result;
}


Values Access::Insert(const string& rel_var_name, const ValueMap& value_map)
{
    static const format empty_cmd("SELECT ku.insert_into_empty('%1%');");
    static const format cmd(
        "INSERT INTO \"%1%\" (%2%) VALUES (%3%) RETURNING *;");
    static const format default_cmd(
        "INSERT INTO \"%1%\" DEFAULT VALUES RETURNING *;");

    db_impl_.GetQuotaController().Check(*work_ptr_);

    const RelVar& rel_var(
        db_impl_.GetManager().GetMeta().GetRelVar(rel_var_name));
    const RichHeader& rich_header(rel_var.GetRichHeader());
    string sql_str;
    unsigned long long size = 0;
    if (rich_header.empty()) {
        if (!value_map.empty())
            throw Error(Error::FIELD,
                        "Non empty insert into zero-column RelVar");
        sql_str = (format(empty_cmd) % rel_var_name).str();
    } else {
        if (!value_map.empty()) {
            BOOST_FOREACH(const ValueMap::value_type& name_value, value_map)
                rich_header.find(name_value.first);
            ostringstream names_oss, values_oss;
            OmitInvoker print_names_sep((SepPrinter(names_oss)));
            OmitInvoker print_values_sep((SepPrinter(values_oss)));
            Quoter quoter(db_impl_.GetConnection());
            BOOST_FOREACH(const RichAttr& rich_attr, rich_header) {
                ValueMap::const_iterator itr(
                    value_map.find(rich_attr.GetName()));
                if (itr == value_map.end()) {
                    if (rich_attr.GetTrait() != Type::SERIAL &&
                        !rich_attr.GetDefaultPtr())
                        throw Error(Error::FIELD,
                                    ("Value of field \"" +
                                     rich_attr.GetName() +
                                     "\" must be supplied"));
                } else {
                    print_names_sep();
                    names_oss << Quoted(rich_attr.GetName());
                    print_values_sep();
                    Value casted_value(itr->second.Cast(rich_attr.GetType()));
                    values_oss << quoter(casted_value.GetPgLiter());
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
            if (default_ptr)
                size += (default_ptr->GetType() == Type::STRING
                         ? default_ptr->GetString().size()
                         : 16); // other types occupy constant amount of space
        }
    }
    pqxx::result pqxx_result;
    pqxx::subtransaction sub_work(*work_ptr_);
    try {
        pqxx_result = sub_work.exec(sql_str);
    } catch (const pqxx::integrity_constraint_violation& err) {
        sub_work.abort();
        throw Error(Error::CONSTRAINT, err.what());
    } catch (const pqxx::data_exception& err) {
        KU_ASSERT(string(err.what()) == "ERROR:  integer out of range\n");
        sub_work.abort();
        throw Error(Error::CONSTRAINT, err.what());
    } catch (const pqxx::sql_error& err) {
        KU_ASSERT(string(err.what()).substr(0, 14) == "ERROR:  Empty ");
        sub_work.abort();
        throw Error(Error::CONSTRAINT, err.what());
    }
    db_impl_.GetQuotaController().DataWereAdded(1, size);
    sub_work.commit();
    if (rich_header.empty())
        return Values();
    KU_ASSERT(pqxx_result.size() == 1);
    return GetTupleValues(pqxx_result[0], rel_var.GetHeader());
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
            KU_ASSERT(tuple.size() == 1);
            result.push_back(tuple[0].as<string>());
        }
        return result;
    }
}


void Access::CheckAppExists(const string& name) const
{
    static const format query("SELECT ku.app_exists(%1%);");
    pqxx::result pqxx_result(
        work_ptr_->exec((format(query) % work_ptr_->quote(name)).str()));
    KU_ASSERT(pqxx_result.size() == 1 && pqxx_result[0].size() == 1);
    if (pqxx_result[0][0].is_null())
        throw NoSuchApp(name);
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


void Access::CheckUserExists(const string& name) const
{
    static const format query("SELECT ku.user_exists(%1%);");
    pqxx::result pqxx_result(
        work_ptr_->exec((format(query) % work_ptr_->quote(name)).str()));
    KU_ASSERT(pqxx_result.size() == 1 && pqxx_result[0].size() == 1);
    if (pqxx_result[0][0].is_null())
        throw Error(Error::NO_SUCH_USER, "No such user: \"" + name + '"');
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
