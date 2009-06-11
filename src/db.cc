
// (c) 2008 by Anton Korenyushkin

/// \file db.cc
/// Database access interface impl

#include "db.h"
#include "querist.h"
#include "translator.h"
#include "utils.h"

#include <boost/format.hpp>

using namespace std;
using namespace ku;
using boost::format;
using boost::scoped_ptr;
using boost::shared_ptr;
using boost::static_visitor;
using boost::apply_visitor;


////////////////////////////////////////////////////////////////////////////////
// Constants
////////////////////////////////////////////////////////////////////////////////

namespace
{
    const size_t MAX_NAME_SIZE = 60;
    const size_t MAX_ATTR_NUMBER = 500;
    const size_t MAX_REL_NUMBER = 500;
}

////////////////////////////////////////////////////////////////////////////////
// Work
////////////////////////////////////////////////////////////////////////////////

namespace
{
    typedef pqxx::transaction<pqxx::serializable> Work;
}

////////////////////////////////////////////////////////////////////////////////
// RichAttr
///////////////////////////////////////////////////////////////////////////////

RichAttr::RichAttr(const std::string& name,
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


const std::string& RichAttr::GetName() const
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
// RelMeta
///////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Relation metadata
    class RelMeta {
    public:
        RelMeta(Work& work, const string& name);
        RelMeta(const string& name,
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
}


RelMeta::RelMeta(Work& work, const string& name)
    : name_(name)
{
    static const format query(
        "SELECT attribute.attname, pg_catalog.pg_type.typname, "
        "       ku.eval(attribute.adsrc) "
        "FROM pg_catalog.pg_type, "
        "(pg_catalog.pg_attribute LEFT JOIN pg_catalog.pg_attrdef "
        " ON pg_catalog.pg_attribute.attrelid = pg_catalog.pg_attrdef.adrelid "
        " AND pg_catalog.pg_attribute.attnum = pg_catalog.pg_attrdef.adnum) "
        " AS attribute "
        "WHERE "
        "pg_catalog.pg_type.oid = attribute.atttypid AND "
        "attribute.attnum > 0 AND "
        "attribute.attrelid = '\"%1%\"'::regclass "
        "ORDER BY attribute.attnum;");

    const pqxx::result query_result =
        work.exec((format(query) % name_).str());
    rich_header_.reserve(query_result.size());
    BOOST_FOREACH(const pqxx::result::tuple& tuple, query_result) {
        KU_ASSERT(tuple.size() == 3);
        KU_ASSERT(!tuple[0].is_null() && ! tuple[1].is_null());
        string name(tuple[0].c_str());
        Type type(PgType(tuple[1].c_str()));
        Type::Trait trait = Type::COMMON;
        auto_ptr<Value> default_ptr;
        if (string(tuple[1].c_str()) == "int4") {
            KU_ASSERT(type == Type::NUMBER);
            trait = Type::INT;
        }
        if (!tuple[2].is_null()) {
            string default_str(tuple[2].c_str());
            if (default_str.substr(0, 8) == "nextval(") {
                KU_ASSERT(type == Type::NUMBER && trait == Type::INT);
                trait = Type::SERIAL;
            } else {
                default_ptr.reset(new Value(type, default_str));
            }
        }
        rich_header_.add_sure(RichAttr(name, type, trait, default_ptr.get()));
    }
    InitHeader();
}


RelMeta::RelMeta(const string& name,
                 const RichHeader& rich_header,
                 const Constrs& constrs)
    : name_(name)
    , rich_header_(rich_header)
    , constrs_(constrs)
{
    InitHeader();
}


string RelMeta::GetName() const
{
    return name_;
}


const RichHeader& RelMeta::GetRichHeader() const
{
    return rich_header_;
}


const Header& RelMeta::GetHeader() const
{
    return header_;
}


const Constrs& RelMeta::GetConstrs() const
{
    return constrs_;
}


Constrs& RelMeta::GetConstrs()
{
    return constrs_;
}


void RelMeta::InitHeader()
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
    /// Database metadata
    class DBMeta {
    public:
        typedef vector<RelMeta> RelMetas;

        DBMeta(Work& work, const string& schema_name);
        const RelMeta& GetRelMeta(const string& rel_name) const;
        const RelMetas& GetRelMetas() const;
        
        void CreateRel(Work& work,
                       const Querist& querist,
                       const string& rel_name,
                       const RichHeader& rich_header,
                       const Constrs& constrs);
        
        void DeleteRels(Work& work, const orset<string>& rel_names);

    private:
        RelMetas rel_metas_;

        size_t GetRelMetaIdx(const string& rel_name) const; // never throws
        size_t GetRelMetaIdxChecked(const string& rel_name) const;
        static void CheckNameSize(const string& name);
    };

    
    /// Constraints loader functor. Works on a whole group.
    class ConstrsLoader {
    public:
        ConstrsLoader(DBMeta::RelMetas& rel_metas);
        void operator()(Work& work) const;

    private:
        DBMeta::RelMetas& rel_metas_;

        void LoadConstrs(Work& work, RelMeta& rel_meta) const;
        
        void SetConstrByPgTuple(RelMeta& rel_meta,
                                const pqxx::result::tuple& tuple) const;
        
        static StringSet GetFieldsByPgArray(const RichHeader& rich_header,
                                            const string& pg_array);
        
        static vector<size_t> ReadPgArray(const string& pg_array);
        const RelMeta& GetRelMetaByName(const string& rel_name) const;
    };
    

    /// Relation creation functor
    class RelCreator {
    public:
        RelCreator(const DBMeta& db_meta, const RelMeta& rel_meta);

        void operator()(Work& work, const Querist& querist) const;

    private:
        const RelMeta& rel_meta_;

        void PrintHeader(ostream& os,
                         OmitInvoker& print_sep,
                         const Quoter& quoter) const;

        void PrintConstrs(const Querist& querist,
                          ostream& os,
                          OmitInvoker& print_sep) const;

        void PrintCreateSequences(ostream& os) const;
        void PrintAlterSequences(ostream& os) const;
    };
    

    /// Relations deletion functor. Works on a group. Requires it not to have
    /// foreign key reference relations with dependents outside the group.
    class RelsDeleter {
    public:
        RelsDeleter(const DBMeta::RelMetas& all_rel_metas,
                    const orset<string>& rel_names_for_del);
        void operator()(Work& work) const;

    private:
        const DBMeta::RelMetas& all_rel_metas_;
        orset<string> rel_names_for_del_;

        void CheckDependencies() const;
        bool IsSetForDelition(const string& rel_name) const;
    };    
}

////////////////////////////////////////////////////////////////////////////////
// DBMeta definitions
////////////////////////////////////////////////////////////////////////////////

DBMeta::DBMeta(Work& work, const string& schema_name)
{
    static const format query(
        "(SELECT viewname AS relname "
        "FROM pg_catalog.pg_views "
        "WHERE schemaname='%1%') "
        "UNION "
        "(SELECT tablename AS relname "
        " FROM pg_catalog.pg_tables "
        " WHERE schemaname='%1%');");

    const pqxx::result query_result =
        work.exec((format(query) % schema_name).str());
    rel_metas_.reserve(query_result.size());
    BOOST_FOREACH(const pqxx::result::tuple& tuple, query_result) {
        KU_ASSERT(tuple.size() == 1);
        KU_ASSERT(!tuple[0].is_null());
        rel_metas_.push_back(RelMeta(work, tuple[0].c_str()));
    }
    ConstrsLoader constrs_loader(rel_metas_);
    constrs_loader(work);
}

const DBMeta::RelMetas& DBMeta::GetRelMetas() const
{
    return rel_metas_;
}


const RelMeta& DBMeta::GetRelMeta(const string& rel_name) const
{
    return rel_metas_[GetRelMetaIdxChecked(rel_name)];
}


void DBMeta::CreateRel(Work& work,
                       const Querist& querist,
                       const string& rel_name,
                       const RichHeader& rich_header,
                       const Constrs& constrs)
{
    if (rel_metas_.size() >= MAX_REL_NUMBER) {
        static const string message(
            (format("Maximum relation number is %1%") % MAX_REL_NUMBER).str());
        throw Error(message);
    }
    CheckNameSize(rel_name);
    if (rich_header.size() > MAX_ATTR_NUMBER) {
        static const string message(
            (format("Maximum attribute number is %1%") %
             MAX_ATTR_NUMBER).str());
        throw Error(message);
    }
    if (GetRelMetaIdx(rel_name) != static_cast<size_t>(-1))
        throw Error("Relation " + rel_name + " already exists");
    BOOST_FOREACH(const RichAttr& rich_attr, rich_header)
        CheckNameSize(rich_attr.GetName());
    RelMeta rel_meta(rel_name, rich_header, constrs);
    if (!rich_header.empty()) {
        StringSet all_field_names;
        all_field_names.reserve(rich_header.size());
        BOOST_FOREACH(const RichAttr& rich_attr, rich_header)
            all_field_names.add_sure(rich_attr.GetName());
        rel_meta.GetConstrs().push_back(Unique(all_field_names));
    }
    RelCreator rel_creator(*this, rel_meta);
    rel_creator(work, querist);
    rel_metas_.push_back(rel_meta);
}


void DBMeta::DeleteRels(Work& work, const orset<string>& rel_names)
{
    RelsDeleter rel_deleter(rel_metas_, rel_names);
    rel_deleter(work);
    BOOST_FOREACH(const string& rel_name, rel_names)
        rel_metas_.erase(rel_metas_.begin() + GetRelMetaIdxChecked(rel_name));
}


size_t DBMeta::GetRelMetaIdx(const string& rel_name) const
{
    for (size_t i = 0; i < rel_metas_.size(); ++i)
        if (rel_metas_[i].GetName() == rel_name)
            return i;
    return -1;
}


size_t DBMeta::GetRelMetaIdxChecked(const string& rel_name) const
{
    size_t result = GetRelMetaIdx(rel_name);
    if (result == static_cast<size_t>(-1))
        throw Error("Relation " + rel_name + " does not exist in metadata");
    return result;
}


void DBMeta::CheckNameSize(const string& name)
{
    if (name.size() > MAX_NAME_SIZE) {
        static const string message (
            (format("Relation and attribute name length must be "
                    "no more than %1% characters") %
             MAX_NAME_SIZE).str());
        throw Error(message);
    }
}

////////////////////////////////////////////////////////////////////////////////
// ConstrsLoader definitions
////////////////////////////////////////////////////////////////////////////////

ConstrsLoader::ConstrsLoader(DBMeta::RelMetas& rel_metas)
    : rel_metas_(rel_metas) {}


void ConstrsLoader::operator()(Work& work) const
{
    BOOST_FOREACH(RelMeta& rel_meta, rel_metas_)
        LoadConstrs(work, rel_meta);
}


void ConstrsLoader::LoadConstrs(Work& work, RelMeta& rel_meta) const
{
    static const format query(
        "SELECT contype, conkey, relname, confkey "
        "FROM pg_catalog.pg_constraint LEFT JOIN pg_catalog.pg_class "
        "ON pg_catalog.pg_constraint.confrelid = pg_catalog.pg_class.oid "
        "WHERE conrelid = '\"%1%\"'::regclass;");

    const pqxx::result query_result =
        work.exec((format(query) % rel_meta.GetName()).str());
    rel_meta.GetConstrs().reserve(query_result.size());
    BOOST_FOREACH(const pqxx::result::tuple& tuple, query_result)
        SetConstrByPgTuple(rel_meta, tuple);
}


void ConstrsLoader::SetConstrByPgTuple(RelMeta& rel_meta,
                                       const pqxx::result::tuple& tuple) const
{
    KU_ASSERT(tuple.size() == 4 && !tuple[0].is_null() && !tuple[1].is_null());
    KU_ASSERT(string(tuple[0].c_str()).size() == 1);
    
    Constrs& constrs(rel_meta.GetConstrs());
    const RichHeader& rich_header(rel_meta.GetRichHeader());
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
            ref_rich_header(GetRelMetaByName(tuple[2].c_str()).GetRichHeader());
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


const RelMeta& ConstrsLoader::GetRelMetaByName(const string& rel_name) const
{
    BOOST_FOREACH(const RelMeta& rel_meta, rel_metas_)
        if (rel_meta.GetName() == rel_name)
            return rel_meta;
    Fail("Constraint to unavaliable relation");
}

////////////////////////////////////////////////////////////////////////////////
// RelCreator definitions
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class ConstrChecker : public static_visitor<void> {
    public:
        ConstrChecker(const DBMeta& db_meta,
                      const RelMeta& rel_meta)
            : db_meta_(db_meta), rel_meta_(rel_meta) {}

        void operator()(const Unique& unique) const {
            if (unique.field_names.empty())
                throw Error("Empty unique field set");
            BOOST_FOREACH(const string& field_name, unique.field_names)
                rel_meta_.GetRichHeader().find(field_name);
        }

        void operator()(const ForeignKey& foreign_key) const {
            if (foreign_key.key_field_names.empty())
                throw Error("Foreign key with empty key field set");
            if (foreign_key.key_field_names.size() !=
                foreign_key.ref_field_names.size())
                throw Error("Ref-key fields size mismatch");

            const RichHeader& key_rich_header(rel_meta_.GetRichHeader());
            const RelMeta&
                ref_rel_meta(foreign_key.ref_rel_name == rel_meta_.GetName()
                             ? rel_meta_
                             : db_meta_.GetRelMeta(foreign_key.ref_rel_name));
            const RichHeader& ref_rich_header(ref_rel_meta.GetRichHeader());
            
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
                    throw Error("Foreign key field types mismatch: " +
                                rel_meta_.GetName() + '.' +
                                key_field_name + " is " +
                                key_field_type.GetKuStr(key_field_trait) +
                                ", " +
                                foreign_key.ref_rel_name + '.' +
                                ref_field_name + " is " +
                                ref_field_type.GetKuStr(ref_field_trait));
            }
            
            BOOST_FOREACH(const Constr& constr, ref_rel_meta.GetConstrs()) {
                const Unique* unique_ptr = boost::get<Unique>(&constr);
                if (unique_ptr &&
                    unique_ptr->field_names == foreign_key.ref_field_names)
                    return;
            }
            throw Error("Foreign key ref fields must be unique");
        }

        void operator()(const Check& /*check*/) const {}

    private:
        const DBMeta& db_meta_;
        const RelMeta& rel_meta_;
    };
}


RelCreator::RelCreator(const DBMeta& db_meta, const RelMeta& rel_meta)
    : rel_meta_(rel_meta)
{
    BOOST_FOREACH(const Constr& constr, rel_meta_.GetConstrs())
        apply_visitor(ConstrChecker(db_meta, rel_meta_), constr);
}


void RelCreator::operator()(Work& work, const Querist& querist) const
{
    ostringstream oss;
    PrintCreateSequences(oss);
    oss << "CREATE TABLE \"" << rel_meta_.GetName() << "\" (";
    OmitInvoker print_sep((SepPrinter(oss)));
    PrintHeader(oss, print_sep, Quoter(work));
    PrintConstrs(querist, oss, print_sep);
    oss << ");";
    PrintAlterSequences(oss);
    work.exec(oss.str());
}


void RelCreator::PrintHeader(ostream& os,
                             OmitInvoker& print_sep,
                             const Quoter& quoter) const
{
    BOOST_FOREACH(const RichAttr& rich_attr, rel_meta_.GetRichHeader()) {
        print_sep();
        os << Quoted(rich_attr.GetName()) << ' '
           << rich_attr.GetType().GetPgStr(rich_attr.GetTrait())
           << " NOT NULL";
        const Value* default_ptr(rich_attr.GetDefaultPtr());
        if (default_ptr)
            os << " DEFAULT " << quoter(default_ptr->GetPgLiter());
        else if (rich_attr.GetTrait() == Type::SERIAL)
            os << " DEFAULT nextval('\""
               << rel_meta_.GetName() << '@' << rich_attr.GetName()
               << "\"')";
    }
}


namespace
{
    class ConstrPrinter : public static_visitor<void> {
    public:
        ConstrPrinter(const Querist& querist,
                      const RelMeta& rel_meta,
                      ostream& os,
                      OmitInvoker& print_sep)
            : querist_(querist)
            , rel_meta_(rel_meta)
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
            os_ << " REFERENCES " << Quoted(foreign_key.ref_rel_name);
            PrintNameSet(foreign_key.ref_field_names);
        }
        
        void operator()(const Check& check) {
            print_sep_();
            os_ << "CHECK ("
                << querist_.TranslateExpr(check.expr_str,
                                          rel_meta_.GetName(),
                                          rel_meta_.GetHeader())
                << ')';
        }

    private:
        const Querist& querist_;
        const RelMeta& rel_meta_;
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


void RelCreator::PrintConstrs(const Querist& querist,
                              ostream& os,
                              OmitInvoker& print_sep) const
{
    ConstrPrinter constr_printer(querist, rel_meta_, os, print_sep);
    BOOST_FOREACH(const Constr& constr, rel_meta_.GetConstrs())
        apply_visitor(constr_printer, constr);
}


void RelCreator::PrintCreateSequences(ostream& os) const
{
    static const format
        cmd("CREATE SEQUENCE \"%1%@%2%\" MINVALUE 0 START 0;");
    
    BOOST_FOREACH(const RichAttr& rich_attr, rel_meta_.GetRichHeader())
        if (rich_attr.GetTrait() == Type::SERIAL)
            os << (format(cmd) % rel_meta_.GetName() % rich_attr.GetName());
}


void RelCreator::PrintAlterSequences(ostream& os) const
{
    static const format
        cmd("ALTER SEQUENCE \"%1%@%2%\" OWNED BY \"%1%\".\"%2%\";");
    
    BOOST_FOREACH(const RichAttr& rich_attr, rel_meta_.GetRichHeader())
        if (rich_attr.GetTrait() == Type::SERIAL)
            os << (format(cmd) % rel_meta_.GetName() % rich_attr.GetName());
}

////////////////////////////////////////////////////////////////////////////////
// RelsDeleter definitions
////////////////////////////////////////////////////////////////////////////////

RelsDeleter::RelsDeleter(const DBMeta::RelMetas& all_rel_metas,
                         const orset<string>& rel_names_for_del)
    : all_rel_metas_(all_rel_metas)
    , rel_names_for_del_(rel_names_for_del)
{
}


void RelsDeleter::operator()(Work& work) const
{
    if (rel_names_for_del_.empty())
        return;
    CheckDependencies();
    ostringstream oss;
    OmitInvoker print_sep((SepPrinter(oss)));
    BOOST_FOREACH(const string& rel_name_for_del, rel_names_for_del_) {
        print_sep();
        oss << Quoted(rel_name_for_del);
    }
    work.exec("DROP TABLE " + oss.str() + " CASCADE;");
}


void RelsDeleter::CheckDependencies() const
{
    BOOST_FOREACH(const RelMeta& rel_meta, all_rel_metas_) {
        if (IsSetForDelition(rel_meta.GetName()))
            continue;
        BOOST_FOREACH(const Constr& constr, rel_meta.GetConstrs()) {
            const ForeignKey* foreign_key_ptr = boost::get<ForeignKey>(&constr);
            if (foreign_key_ptr &&
                IsSetForDelition(foreign_key_ptr->ref_rel_name))
                throw Error("Attempt to delete a group of relations "
                            "with a relation " +
                            foreign_key_ptr->ref_rel_name +
                            " but without a relation " +
                            rel_meta.GetName() +
                            " it is dependent on");
        }
    }
}


bool RelsDeleter::IsSetForDelition(const string& rel_name) const
{
    BOOST_FOREACH(const string& rel_name_for_del, rel_names_for_del_)
        if (rel_name == rel_name_for_del)
            return true;
    return false;
}

////////////////////////////////////////////////////////////////////////////////
// ConsistController
////////////////////////////////////////////////////////////////////////////////

/// Class for controlling the consistency of metadata cache.
/// All member functions never throw.
class ConsistController {
public:
    class Guard {
    public:
        Guard(ConsistController& cc)
            : cc_(cc), commited_(false) {}

        ~Guard() {
            if (!commited_ && cc_.changed_)
                cc_.consistent_ = false;
        }
        
        void CommitHappened() {
            commited_ = true;
        }

    private:
        ConsistController& cc_;
        bool commited_;
    };

    ConsistController()
        : consistent_(false) {}
    
    void ChangeHappened() {
        changed_ = true;
    }
    
    bool IsConsistent() const {
        return consistent_;
    }
    
    void SyncHappened() {
        consistent_ = true;
        changed_ = false;
    }

private:
    bool changed_;
    bool consistent_;
};

////////////////////////////////////////////////////////////////////////////////
// Manager
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Metadata manager. Maintains metadata consistensy.
    class Manager {
    public:
        Manager(Work& work,
                const string& schema_name,
                ConsistController& cc);
        const DBMeta& GetMeta() const;
        DBMeta& ChangeMeta();
        void LoadMeta(Work& work);
    
    private:
        boost::scoped_ptr<DBMeta> meta_;
        string schema_name_;
        ConsistController& consist_controller_;

        void RetrieveMeta();
    };
}

#include <iostream>
Manager::Manager(Work& work, const string& schema_name, ConsistController& cc)
    : schema_name_(schema_name)
    , consist_controller_(cc)
{
    static const format cmd("SET search_path TO \"%1%\", pg_catalog;");

    work.exec((format(cmd) % schema_name).str());
    LoadMeta(work);
}


void Manager::LoadMeta(Work& work)
{
    meta_.reset(new DBMeta(work, schema_name_));
    consist_controller_.SyncHappened();
}


const DBMeta& Manager::GetMeta() const
{
    return *meta_;
}


DBMeta& Manager::ChangeMeta()
{
    consist_controller_.ChangeHappened();
    return *meta_;
}

////////////////////////////////////////////////////////////////////////////////
// DBViewerImpl
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Implementation of DBViewer providing access to database for translator
    class DBViewerImpl : public DBViewer {
    public:
        DBViewerImpl(const Manager& manager, const Quoter& quoter);
        virtual const Header& GetRelHeader(const string& rel_name) const;
        virtual string Quote(const PgLiter& pg_liter) const;
        virtual RelFields GetReference(const RelFields& key) const;
        
    private:
        const Manager& manager_;
        Quoter quoter_;

        Error MakeKeyError(const RelFields& key, const string& message) const;
    };
}


DBViewerImpl::DBViewerImpl(const Manager& manager, const Quoter& quoter)
    : manager_(manager), quoter_(quoter)
{
}


const Header& DBViewerImpl::GetRelHeader(const std::string& rel_name) const
{
    return manager_.GetMeta().GetRelMeta(rel_name).GetHeader();
}


std::string DBViewerImpl::Quote(const PgLiter& pg_liter) const
{
    return quoter_(pg_liter);
}


DBViewer::RelFields DBViewerImpl::GetReference(const RelFields& key) const
{
    const RelMeta& rel_meta(manager_.GetMeta().GetRelMeta(key.rel_name));
    const ForeignKey* found_foreign_key_ptr = 0;
    BOOST_FOREACH(const Constr& constr, rel_meta.GetConstrs()) {
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
        return RelFields(found_foreign_key_ptr->ref_rel_name,
                         found_foreign_key_ptr->ref_field_names);
    else
        throw MakeKeyError(key, "doesn't have a key with fields");
}


Error DBViewerImpl::MakeKeyError(const RelFields& key,
                                 const string& message) const
{
    ostringstream oss;
    OmitInvoker print_sep((SepPrinter(oss)));
    BOOST_FOREACH(const string& field, key.field_names) {
        print_sep();
        oss << field;
    }
    return Error("Relation " + key.rel_name + ' ' + message + ' ' + oss.str());
}

////////////////////////////////////////////////////////////////////////////////
// Access::Data
////////////////////////////////////////////////////////////////////////////////

/// Data each Transactor has for DB operations
struct ku::Access::Data {
    Manager& manager;
    Querist& querist;
    Quoter quoter;
    Work& work;

    Data(Manager& manager,
         Querist& querist,
         const Quoter& quoter,
         Work& work)
        : manager(manager)
        , querist(querist)
        , quoter(quoter)
        , work(work) {}
};

////////////////////////////////////////////////////////////////////////////////
// DB::Impl
////////////////////////////////////////////////////////////////////////////////

/// Connection holder and transaction manager
class DB::Impl {
public:
    Impl(const string& opt,
         const string& schema_name,
         int try_count = 3);
    void Perform(Transactor& transactor);

private:
    pqxx::connection conn_;
    int try_count_;
    ConsistController consist_controller_;
    scoped_ptr<Manager> manager_ptr_;
    scoped_ptr<DBViewerImpl> db_viewer_ptr_;
    scoped_ptr<Querist> querist_ptr_;
};


DB::Impl::Impl(const string& opt,
               const string& schema_name,
               int try_count)
    : conn_(opt)
    , try_count_(try_count)
{
    conn_.set_noticer(auto_ptr<pqxx::noticer>(new pqxx::nonnoticer()));
    {
        // TODO: may be should be implemented through Transactor
        Work work(conn_);
        manager_ptr_.reset(new Manager(work, schema_name, consist_controller_));
        work.commit(); // don't remove it, stupid idiot! is sets search_path!
    }
    db_viewer_ptr_.reset(new DBViewerImpl(*manager_ptr_, Quoter(conn_)));
    querist_ptr_.reset(new Querist(*db_viewer_ptr_, conn_));
}


void DB::Impl::Perform(Transactor& transactor) {
    for (int i = try_count_; ; --i) {
        Work work(conn_);
        Access::Data access_data(*manager_ptr_,
                                 *querist_ptr_,
                                 Quoter(conn_),
                                 work);
        Access access(access_data);
        try {
            if (!consist_controller_.IsConsistent()) {
                manager_ptr_->LoadMeta(work);
                querist_ptr_->ClearCache();
            }
            ConsistController::Guard consist_guard(consist_controller_);
            transactor(access);
            work.commit();
            consist_guard.CommitHappened();
            return;
        } catch (const Error&) {
            throw;
        } catch (const pqxx::pqxx_exception& err) {
            if (i <= 0)
                Fail(err.base().what());
            transactor.Reset();
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// DB definitions
////////////////////////////////////////////////////////////////////////////////

DB::DB(const std::string& opt, const std::string& schema_name)
{
    try {
        pimpl_.reset(new Impl(opt, schema_name));
    } catch (const pqxx::pqxx_exception& err) {
        Fail(err.base().what());
    }   
}


DB::~DB()
{
}


void DB::Perform(Transactor& transactor)
{
    pimpl_->Perform(transactor);
}

////////////////////////////////////////////////////////////////////////////////
// Access definitions
////////////////////////////////////////////////////////////////////////////////

Access::Access(Data& data)
    : data_(data)
{
}


bool Access::HasRel(const std::string& rel_name) const
{
    const DBMeta::RelMetas& rel_metas(data_.manager.GetMeta().GetRelMetas());
    BOOST_FOREACH(const RelMeta& rel_meta, rel_metas)
        if (rel_meta.GetName() == rel_name)
            return true;
    return false;
}


StringSet Access::GetRelNames() const
{
    const DBMeta::RelMetas& rel_metas(data_.manager.GetMeta().GetRelMetas());
    StringSet result;
    result.reserve(rel_metas.size());
    BOOST_FOREACH(const RelMeta& rel_meta, rel_metas)
        result.add_sure(rel_meta.GetName());
    return result;
}


const RichHeader& Access::GetRelRichHeader(const string& rel_name) const
{
    return data_.manager.GetMeta().GetRelMeta(rel_name).GetRichHeader();    
}


const Constrs& Access::GetRelConstrs(const std::string& rel_name) const
{
    return data_.manager.GetMeta().GetRelMeta(rel_name).GetConstrs();
}


void Access::CreateRel(const string& name,
                       const RichHeader& rich_header,
                       const Constrs& constrs)
{
    data_.manager.ChangeMeta().CreateRel(data_.work, data_.querist,
                                         name, rich_header, constrs);
}


void Access::DeleteRels(const StringSet& rel_names)
{
    data_.manager.ChangeMeta().DeleteRels(data_.work, rel_names);
}


void Access::DeleteRel(const string& rel_name)
{
    StringSet rel_names;
    rel_names.add_sure(rel_name);
    DeleteRels(rel_names);
}


QueryResult Access::Query(const std::string& query_str,
                          const Values& params,
                          const Specifiers& specifiers) const
{
    return data_.querist.Query(data_.work, query_str, params, specifiers);
}


unsigned long Access::Update(const std::string& rel_name,
                             const StringMap& field_expr_map,
                             const Values& params,
                             const WhereSpecifiers& where_specifiers)
{
    const RichHeader& rich_header(GetRelRichHeader(rel_name));
    BOOST_FOREACH(const StringMap::value_type& field_expr, field_expr_map)
        rich_header.find(field_expr.first);
    pqxx::subtransaction sub_work(data_.work);
    try {
        unsigned long result = data_.querist.Update(sub_work, rel_name,
                                                    field_expr_map,
                                                    params, where_specifiers);
        sub_work.commit();
        return result;
    } catch (const pqxx::integrity_constraint_violation& err) {
        sub_work.abort();
        throw Error(err.what());
    }
}


unsigned long Access::Delete(const std::string& rel_name,
                             const WhereSpecifiers& where_specifiers)
{
    pqxx::subtransaction sub_work(data_.work);
    try {
        unsigned long result = data_.querist.Delete(sub_work, rel_name,
                                                    where_specifiers);
        sub_work.commit();
        return result;
    } catch (const pqxx::integrity_constraint_violation& err) {
        sub_work.abort();
        throw Error(err.what());
    }    
}    


Values Access::Insert(const std::string& rel_name, const ValueMap& value_map)
{
    static const format empty_cmd("SELECT ku.insert_into_empty('%1%');");
    static const format cmd(
        "INSERT INTO \"%1%\" (%2%) VALUES (%3%) RETURNING *;");
    static const format default_cmd(
        "INSERT INTO \"%1%\" DEFAULT VALUES RETURNING *;");

    const RelMeta& rel_meta(data_.manager.GetMeta().GetRelMeta(rel_name));
    const RichHeader& rich_header(rel_meta.GetRichHeader());
    string sql_str;
    if (rich_header.empty()) {
        if (!value_map.empty())
            throw Error("Non empty insert into zero-column relation");
        sql_str = (format(empty_cmd) % rel_name).str();
    } else if (!value_map.empty()) {
        BOOST_FOREACH(const ValueMap::value_type& name_value, value_map)
            rich_header.find(name_value.first);
        ostringstream names_oss, values_oss;
        OmitInvoker print_names_sep((SepPrinter(names_oss)));
        OmitInvoker print_values_sep((SepPrinter(values_oss)));
        BOOST_FOREACH(const RichAttr& rich_attr, rich_header) {
            ValueMap::const_iterator itr(value_map.find(rich_attr.GetName()));
            if (itr == value_map.end()) {
                if (rich_attr.GetTrait() != Type::SERIAL &&
                    !rich_attr.GetDefaultPtr())
                    throw Error("Value of field " +
                                rich_attr.GetName() +
                                " must be supplied");
            } else {
                print_names_sep();
                names_oss << Quoted(rich_attr.GetName());
                print_values_sep();
                Value casted_value(itr->second.Cast(rich_attr.GetType()));
                values_oss << data_.quoter(casted_value.GetPgLiter());
            }
        }
        sql_str = (format(cmd)
                   % rel_name
                   % names_oss.str()
                   % values_oss.str()).str();
    } else {
        sql_str = (format(default_cmd) % rel_name).str();
    }
    pqxx::subtransaction sub_work(data_.work);
    try {
        pqxx::result pqxx_result(sub_work.exec(sql_str));
        sub_work.commit();
        if (rich_header.empty())
            return Values();
        KU_ASSERT(pqxx_result.size() == 1);
        return GetTupleValues(pqxx_result[0], rel_meta.GetHeader());
    } catch (const pqxx::integrity_constraint_violation& err) {
        sub_work.abort();
        throw Error(err.what());
    } catch (const pqxx::sql_error& err) {
        KU_ASSERT(string(err.what()).substr(0, 14) == "ERROR:  Empty ");
        sub_work.abort();
        throw Error(err.what());
    }        
}
