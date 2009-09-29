
// (c) 2008 by Anton Korenyushkin

/// \file test-main.cc
/// Test entry point

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Main


#include "../src/parser.h"
#include "../src/db.h"
#include "../src/translator.h"
#include "../src/js.h"

#include <boost/test/unit_test.hpp>
#include <boost/foreach.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <pqxx/connection>

#include <fstream>

using namespace std;
using namespace ku;
using boost::bind;
using boost::function;
using boost::lexical_cast;
using boost::noncopyable;


////////////////////////////////////////////////////////////////////////////////
// TestReader
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Class for reading test pairs from a file
    class TestReader {
    public:
        typedef pair<string, string> Pair;
        typedef vector<Pair> Data;
        
        TestReader(istream& is)
            : is_(is) {}

        Data operator()();

    private:
        istream& is_;
        
        string ReadUpTo(const string& terminator);
        static bool Begins(const string& str, const string& prefix);
    };
}


TestReader::Data TestReader::operator()()
{
    Data result;
    while (!is_.eof()) {
        string line;
        getline(is_, line);
        if (line.empty() || Begins(line, "*#"))
            continue;
        BOOST_REQUIRE(Begins(line, "*("));
        Pair test_pair;
        test_pair.first = ReadUpTo("**");
        test_pair.second = ReadUpTo("*)");
        result.push_back(test_pair);
    }
    return result;
}


string TestReader::ReadUpTo(const string& terminator)
{
    string result;
    for (;;) {
        string line;
        getline(is_, line);
        BOOST_REQUIRE(!is_.eof());
        if (Begins(line, "*#"))
            continue;
        if (Begins(line, terminator))
            break;
        result += line +'\n';
    }
    return result;
}


bool TestReader::Begins(const string& str, const string& prefix)
{
    return str.substr(0, prefix.size()) == prefix;
}

////////////////////////////////////////////////////////////////////////////////
// FileTester
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Class transforming parts of test pairs into type T
    /// and comparing results
    template <typename T, typename CompT = equal_to<T> >
    class FileTester {
    public:
        typedef function<T (const string&)> Transformer;
        
        FileTester(const string& file_name,
                   const Transformer& trans1,
                   const Transformer& trans2)
            : file_name_(file_name)
            , trans1_(trans1)
            , trans2_(trans2) {}

        void operator()() {
            ifstream file(file_name_.c_str());
            TestReader::Data data((TestReader(file)()));
            BOOST_FOREACH(const TestReader::Pair& test_pair, data) {
                T item1 = trans1_(test_pair.first);
                T item2 = trans2_(test_pair.second);
                BOOST_CHECK_MESSAGE(CompT()(item1, item2),
                                    "\n*** ERROR ***\n" +
                                    lexical_cast<string>(item1) +
                                    "\n*** IS NOT EQUAL TO THE SAMPLE ***\n" +
                                    lexical_cast<string>(item2) +
                                    "\n*** END ***\n");
            }
        }            
        
    private:
        string file_name_;
        Transformer trans1_, trans2_;
    };
}

////////////////////////////////////////////////////////////////////////////////
// TestFileStr stuff
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// String null transformation
    string NullTransform(const string& str)
    {
        return str;
    }
    

    /// Word-by-word string comparison
    class WordComparator : public binary_function<string, string, bool> {
    public:
        bool operator()(const string& str1, const string& str2) const {
            stringstream is1(str1), is2(str2);
            istream_iterator<string> end;
            vector<string> words1(istream_iterator<string>(is1), end);
            vector<string> words2(istream_iterator<string>(is2), end);
            return words1 == words2;
        }
    };


    /// Functor adaptor (returns printed result)
    template <typename Func>
    struct StrFunctor {
        Func f;
        StrFunctor(Func f) : f(f) {}
        string operator()(const string& str) {
            ostringstream os;
            os << f(str);
            return os.str();
        }
    };


    /// StrFunctor instantiator
    template <typename Func>
    StrFunctor<Func> StrFunc(Func f)
    {
        return StrFunctor<Func>(f);
    }


    /// Routine for common case of string based file tests
    void TestFileStr(const string& file_name,
                     const function<string (const string&)>& trans1)
    {
        return FileTester<string, WordComparator>(file_name,
                                                  trans1,
                                                  &NullTransform)();
    }
}

////////////////////////////////////////////////////////////////////////////////
// orset test
////////////////////////////////////////////////////////////////////////////////

BOOST_AUTO_TEST_CASE(orset_test)
{
    orset<int> a;
    a.add_unsure(1);
    a.add_unsure(2);
    a.add_unsure(3);
    a.add_unsure(2);
    a.add_unsure(1);
    a.add_unsure(4);
    vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.push_back(4);
    BOOST_CHECK(vector<int>(a.begin(), a.end()) == v);
    orset<int> b(v.rbegin(), v.rend());
    BOOST_CHECK(a == b);
    BOOST_CHECK(vector<int>(a.begin(), a.end()) ==
                vector<int>(b.rbegin(), b.rend()));
    v.push_back(1);
    v.push_back(4);
    BOOST_CHECK(a == orset<int>(v.begin(), v.end(), false));
    b.add_sure(5);
    BOOST_CHECK(a != b);
    BOOST_CHECK_THROW(a.find(42), runtime_error);
    BOOST_CHECK_THROW(const_cast<const orset<int>&>(a).find(42), runtime_error);
}

////////////////////////////////////////////////////////////////////////////////
// Parser test
////////////////////////////////////////////////////////////////////////////////

BOOST_AUTO_TEST_CASE(parser_test)
{
    TestFileStr("test/expr-parser.test", StrFunc(&ParseExpr));
    TestFileStr("test/rel-parser.test", StrFunc(&ParseRel));
    BOOST_CHECK_THROW(ParseExpr("$ 1"), Error);
    BOOST_CHECK_THROW(ParseRel("1"), Error);
    BOOST_CHECK_THROW(ParseRel("for (x, x in r) x"), Error);
    BOOST_CHECK_THROW(ParseExpr("foreach (a, a) true"), Error);
    BOOST_CHECK_THROW(ParseExpr("foreach (b, b in c) true"), Error);
}

////////////////////////////////////////////////////////////////////////////////
// Comparison stuff
////////////////////////////////////////////////////////////////////////////////

namespace ku
{
    bool operator==(const Constrs& lhs, const Constrs& rhs)
    {
        return (orset<Constr>(lhs.begin(), lhs.end(), false) ==
                orset<Constr>(rhs.begin(), rhs.end(), false));
    }

    
    bool operator==(const Value& lhs, const Value& rhs)
    {
        return (lhs.GetType() == rhs.GetType() &&
                lhs.GetString() == rhs.GetString());
    }


    bool operator==(const RichAttr& lhs, const RichAttr& rhs)
    {
        if (lhs.GetName() != rhs.GetName() ||
            lhs.GetType() != rhs.GetType() ||
            lhs.GetTrait() != rhs.GetTrait())
            return false;
        if (!lhs.GetDefaultPtr() && !rhs.GetDefaultPtr())
            return true;
        if (!lhs.GetDefaultPtr() || !rhs.GetDefaultPtr())
            return false;
        return *lhs.GetDefaultPtr() == *rhs.GetDefaultPtr();
    }
}


////////////////////////////////////////////////////////////////////////////////
// Table
////////////////////////////////////////////////////////////////////////////////

namespace
{
    typedef orset<Values> ValuesSet;
        
    /// In-memory relation representation
    class Table {
    public:
        Table(const RichHeader& rich_header,
              const Constrs& constrs = Constrs());
        Table(const Header& header);
        
        Table(istream& is);
        bool operator==(const Table& other) const;
        bool operator!=(const Table& other) const { return !(*this == other); }
        friend ostream& operator<<(ostream& os, const Table& table);
        const ValuesSet& GetValuesSet() const;
        const RichHeader& GetRichHeader() const;
        const Constrs& GetConstrs() const;

        void SetRichHeader(const RichHeader& rich_header);
        void SetConstrs(const Constrs& constrs);
        void AddRow(const Values& values);
        
    private:
        RichHeader rich_header_;
        Constrs constrs_;
        ValuesSet values_set_;

        void ReadHeader(istream& is);
        static Value ReadValue(istream& is, const Type& type);
        void ReadMetaData(istream& is);
        void ReadValuesSet(istream& is);
        void PrintHeader(ostream& os) const;
        void PrintValuesSet(ostream& os) const;
        void AddAllUniqueConstr();
    };


    ostream& operator<<(ostream& os, const Table& table)
    {
        table.PrintHeader(os);
        os << "---\n";
        table.PrintValuesSet(os);
        return os;
    }


    Table ReadTableFromFile(const string& file_name)
    {
        ifstream file(file_name.c_str());
        return Table(file);
    }

    
    Table ReadTableFromString(const string& str)
    {
        istringstream iss(str);
        return Table(iss);
    }
}


Table::Table(const RichHeader& rich_header, const Constrs& constrs)
    : rich_header_(rich_header)
    , constrs_(constrs)
{
    AddAllUniqueConstr();
}


Table::Table(const Header& header)
{
    rich_header_.reserve(header.size());
    BOOST_FOREACH(const Attr& attr, header)
        rich_header_.add_sure(RichAttr(attr.GetName(), attr.GetType()));
    AddAllUniqueConstr();
}       


Table::Table(istream& is)
{
    ReadHeader(is);
    ReadMetaData(is);
    AddAllUniqueConstr();
    ReadValuesSet(is);
}


bool Table::operator==(const Table& other) const
{
    return (rich_header_ == other.rich_header_ &&
            constrs_ == other.constrs_ &&
            values_set_ == other.values_set_);
}


const ValuesSet& Table::GetValuesSet() const
{
    return values_set_;
}


const RichHeader& Table::GetRichHeader() const
{
    return rich_header_;
}


const Constrs& Table::GetConstrs() const
{
    return constrs_;
}


void Table::SetRichHeader(const RichHeader& rich_header)
{
    rich_header_ = rich_header;
}


void Table::SetConstrs(const Constrs& constrs)
{
    constrs_ = constrs;
}


void Table::AddRow(const Values& values)
{
    values_set_.add_sure(values);
}


void Table::ReadHeader(istream& is)
{
    string names_line, types_line;
    getline(is, names_line);
    getline(is, types_line);
    BOOST_CHECK(!names_line.empty() && ! types_line.empty());
    istringstream names_iss(names_line), types_iss(types_line);
    for (;;) {
        string name, type;
        names_iss >> name;
        types_iss >> type;
        if (name.empty() && type.empty())
            break;
        BOOST_CHECK(!name.empty() && ! type.empty());
        rich_header_.add_sure(RichAttr(name, KuType(type)));
    }
}


Value Table::ReadValue(istream& is, const Type& type)
{
    if (type == Type::NUMBER) {
        double d;
        is >> d;
        return Value(type, d);
    } else if (type == Type::STRING) {
        string s;
        is >> s;
        return Value(type, s);
    } else if (type == Type::BOOLEAN) {
        string s;
        is >> s;
        BOOST_CHECK(s == "true" || s == "false");
        return Value(type, s == "true");
    } else {
        BOOST_CHECK(type == Type::DATE);
        string date, time;
        is >> date >> time;
        return Value(type, date + ' ' + time);
    }        
}


void Table::ReadMetaData(istream& is)
{
    for (;;) {
        string line;
        getline(is, line);
        if (line.empty())
            continue;
        if (line.substr(0, 3) == "---")
            break;
        BOOST_REQUIRE_MESSAGE(!is.eof(), "Unexpected end of stream");
        istringstream line_iss(line);
        string constr_name;
        line_iss >> constr_name;
        if (constr_name == "check") {
            constrs_.push_back(Check(line.substr(string("check").size() + 1)));
        } else if (constr_name == "fk") {
            StringSet key_field_names;
            for (;;) {
                string name;
                line_iss >> name;
                BOOST_REQUIRE(!line_iss.eof());
                if (name == "-")
                    break;
                key_field_names.add_sure(name);
            }
            string ref_rel_name;
            line_iss >> ref_rel_name;
            string delimiter;
            line_iss >> delimiter;
            BOOST_REQUIRE(delimiter == "-");
            StringSet ref_field_names;
            for (;;) {
                string name;
                line_iss >> name;
                if (name.empty())
                    break;
                ref_field_names.add_sure(name);
            }
            constrs_.push_back(ForeignKey(key_field_names,
                                          ref_rel_name,
                                          ref_field_names));
        } else if (constr_name == "unique") {
            StringSet field_names;
            for (;;) {
                string name;
                line_iss >> name;
                if (name.empty())
                    break;
                field_names.add_sure(name);
            }
            constrs_.push_back(Unique(field_names));
        } else if (constr_name == "default") {
            string field_name;
            line_iss >> field_name;
            RichAttr& rich_attr(rich_header_.find(field_name));
            Value value(ReadValue(line_iss, rich_attr.GetType()));
            rich_attr = RichAttr(rich_attr.GetName(),
                                 rich_attr.GetType(),
                                 rich_attr.GetTrait(),
                                 &value);
        } else if (constr_name == "int" || constr_name == "serial") {
            string field_name;
            line_iss >> field_name;
            RichAttr& rich_attr(rich_header_.find(field_name));
            rich_attr = RichAttr(rich_attr.GetName(),
                                 rich_attr.GetType(),
                                 constr_name == "int"
                                 ? Type::INT
                                 : Type::SERIAL,
                                 rich_attr.GetDefaultPtr());
        } else {
            BOOST_FAIL("Unknown constraint: " + constr_name);
        }
    }
}


void Table::ReadValuesSet(istream& is)
{
    while (!is.eof()) {
        string line;
        getline(is, line);
        if (line.empty())
            continue;
        istringstream iss(line);
        iss.exceptions (ios::failbit | ios::badbit);
        Values values;
        values.reserve(rich_header_.size());
        BOOST_FOREACH(const RichAttr& rich_attr, rich_header_)
            values.push_back(ReadValue(iss, rich_attr.GetType()));
        values_set_.add_sure(values);
    }    
}


void Table::PrintHeader(ostream& os) const
{
    BOOST_FOREACH(const RichAttr& rich_attr, rich_header_)
        os << rich_attr.GetName() << ' ';
    os << '\n';
    BOOST_FOREACH(const RichAttr& rich_attr, rich_header_)
        os << rich_attr.GetType().GetKuStr() << ' ';
    os << '\n';
}


void Table::PrintValuesSet(ostream& os) const
{
    BOOST_FOREACH(const Values& values, values_set_) {
        BOOST_FOREACH(const Value& value, values)
            os << value.GetString() << ' ';
        os << '\n';
    }
}


void Table::AddAllUniqueConstr()
{
    if (rich_header_.empty())
        return;
    StringSet all_field_names;
    all_field_names.reserve(rich_header_.size());
    BOOST_FOREACH(const RichAttr& rich_attr, rich_header_)
        all_field_names.add_sure(rich_attr.GetName());
    constrs_.push_back(Unique(all_field_names));
}

////////////////////////////////////////////////////////////////////////////////
// DBFixture
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class RelCreator : public Transactor {
    public:
        RelCreator(const string& rel_name, const Table& table)
            : rel_name_(rel_name), table_(table) {}

        virtual void operator()(Access& access) {
            const RichHeader& rich_header(table_.GetRichHeader());
            access.CreateRel(rel_name_,
                             rich_header,
                             table_.GetConstrs());
            BOOST_FOREACH(const Values& values, table_.GetValuesSet()) {
                assert(values.size() == rich_header.size());
                ValueMap value_map;
                for (size_t i = 0; i < rich_header.size(); ++i)
                    value_map.insert(
                        ValueMap::value_type(rich_header[i].GetName(),
                                             values[i]));
                access.Insert(rel_name_, value_map);
            }
        }

    private:
        string rel_name_;
        Table table_;
    };

    
    class Querier : public Transactor {
    public:
        Querier(const string& query_str,
                const Values& params = Values(),
                const Specifiers& specifiers = Specifiers())
            : query_str_(query_str)
            , params_(params)
            , specifiers_(specifiers) {}
        
        virtual void operator()(Access& access) {
            query_result_ptr_.reset(
                new QueryResult(access.Query(query_str_,
                                             params_,
                                             specifiers_)));
        }

        Table GetResult() const {
            BOOST_REQUIRE(query_result_ptr_.get());
            Table result(query_result_ptr_->GetHeader());
            for (size_t idx = 0; idx < query_result_ptr_->GetSize(); ++idx)
                result.AddRow(*query_result_ptr_->GetValuesPtr(idx));
            return result;
        }

    private:
        string query_str_;
        Values params_;
        Specifiers specifiers_;
        auto_ptr<QueryResult> query_result_ptr_;
    };
    

    class RelDumper : public Transactor {
    public:
        RelDumper(const string& rel_name)
            : rel_name_(rel_name)
            , querier_(rel_name)
            , rich_header_ptr_(0)
            , constrs_ptr_(0) {}

        virtual void operator()(Access& access) {
            querier_(access);
            constrs_ptr_ = &access.GetRelConstrs(rel_name_);
            rich_header_ptr_ = &access.GetRelRichHeader(rel_name_);
        }

        Table GetTable() const {
            Table result(querier_.GetResult());
            BOOST_REQUIRE(rich_header_ptr_ && constrs_ptr_);
            result.SetRichHeader(*rich_header_ptr_);
            result.SetConstrs(*constrs_ptr_);
            return result;
        }

        const Constrs& GetConstrs() const {
            BOOST_REQUIRE(constrs_ptr_);
            return *constrs_ptr_;
        }
        
        const RichHeader& GetRichHeader() const {
            BOOST_REQUIRE(rich_header_ptr_);
            return *rich_header_ptr_;
        }

    private:
        string rel_name_;
        Querier querier_;
        const RichHeader* rich_header_ptr_;
        const Constrs* constrs_ptr_;
    };


    class ValuesInserter : public Transactor {
    public:
        ValuesInserter(const string& rel_name, const Values& values)
            : rel_name_(rel_name), values_(values) {}

        virtual void operator()(Access& access) {
            const RichHeader& rich_header(access.GetRelRichHeader(rel_name_));
            assert(values_.size() == rich_header.size());
            ValueMap value_map;
            for (size_t i = 0; i < rich_header.size(); ++i)
                value_map.insert(ValueMap::value_type(rich_header[i].GetName(),
                                                      values_[i]));
            access.Insert(rel_name_, value_map);
        }

    private:
        string rel_name_;
        Values values_;
    };    

    
    class RelsDeleter : public Transactor {
    public:
        RelsDeleter(const StringSet& rel_names)
            : rel_names_(rel_names) {}
        
        virtual void operator()(Access& access) {
            access.DeleteRels(rel_names_);
        }

    private:
        StringSet rel_names_;
    };


    class RelNamesGetter : public Transactor {
    public:
        virtual void operator()(Access& access) {
            result_ = access.GetRelNames();
        }

        const StringSet& GetResult() const {
            return result_;
        }

    private:
        StringSet result_;
    };


    class DBFixture : private noncopyable {
    public:
        DB db;
        
        DBFixture();
        void LoadRelFromFile(const string& rel_name);
        void LoadRelFromString(const string& rel_name, const string& str);
        Table Query(const string& query);
        Table ComplexQuery(const string& query,
                           const Values& params,
                           const Specifiers& specifiers);
        Table DumpRel(const string& rel_name);
        void InsertValues(const string& rel_name, const Values& values);
        void CreateRel(const string& rel_name, const Table& table);
        StringSet GetRelNames();
        const RichHeader& GetRelRichHeader(const string& rel_name);
        void DeleteRels(const StringSet& rel_names);
        const Constrs& GetRelConstrs(const string& rel_name);
    };
}


DBFixture::DBFixture()
    : db("dbname=test_patsak password=1q2w3e", "test")
{
    DeleteRels(GetRelNames());
    BOOST_REQUIRE(GetRelNames().empty());
}


void DBFixture::LoadRelFromFile(const string& rel_name)
{
    CreateRel(rel_name, ReadTableFromFile("test/" + rel_name + ".table"));
}


void DBFixture::LoadRelFromString(const string& rel_name, const string& str)
{
    CreateRel(rel_name, ReadTableFromString(str));
}


Table DBFixture::Query(const string& query)
{
    return ComplexQuery(query, Values(), Specifiers());
}


Table DBFixture::ComplexQuery(const string& query,
                              const Values& params,
                              const Specifiers& specifiers)
{
    Querier querier(query, params, specifiers);
    db.Perform(querier);
    return querier.GetResult();
}


Table DBFixture::DumpRel(const string& rel_name)
{
    RelDumper rel_dumper(rel_name);
    db.Perform(rel_dumper);
    return rel_dumper.GetTable();
}


void DBFixture::InsertValues(const string& rel_name, const Values& values)
{
    ValuesInserter values_inserter(rel_name, values);
    db.Perform(values_inserter);
}


void DBFixture::CreateRel(const string& rel_name, const Table& table)
{
    RelCreator rel_creator(rel_name, table);
    db.Perform(rel_creator);
    BOOST_CHECK(DumpRel(rel_name) == table);
}


StringSet DBFixture::GetRelNames()
{
    RelNamesGetter rel_names_getter;
    db.Perform(rel_names_getter);
    return rel_names_getter.GetResult();
}


const RichHeader& DBFixture::GetRelRichHeader(const string& rel_name)
{
    RelDumper rel_dumper(rel_name);
    db.Perform(rel_dumper);
    return rel_dumper.GetRichHeader();
}


void DBFixture::DeleteRels(const StringSet& rel_names)
{
    RelsDeleter rels_deleter(rel_names);
    db.Perform(rels_deleter);
}


const Constrs& DBFixture::GetRelConstrs(const string& rel_name)
{
    RelDumper rel_dumper(rel_name);
    db.Perform(rel_dumper);
    return rel_dumper.GetConstrs();
}

////////////////////////////////////////////////////////////////////////////////
// DB test
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class BadTransactor : public Transactor {
    public:
        BadTransactor(const string& good_name, const string& bad_name)
            : good_name_(good_name), bad_name_(bad_name) {}
        
        virtual void operator()(Access& access) {
            access.DeleteRel(good_name_);
            access.GetRelRichHeader(bad_name_); // should fail
        }

    private:
        string good_name_;
        string bad_name_;
    };


    class ThrowingTransactor : public Transactor {
    public:
        ThrowingTransactor(int throw_count, const string& rel_name)
            : throw_count_(throw_count)
            , count_(0)
            , rel_name_(rel_name) {}

        virtual void operator()(Access& access) {
            ++count_;
            if (throw_count_-- > 0) {
                access.DeleteRel(rel_name_);
                throw pqxx::failure("Must never appear");
            }
        }

        virtual void Reset() {
            count_ = 0;
        }

        int GetCount() const {
            return count_;
        }

    private:
        int throw_count_, count_;
        string rel_name_;
    };
}


BOOST_FIXTURE_TEST_CASE(db_test, DBFixture)
{
    LoadRelFromFile("User");
    LoadRelFromFile("Post");
    LoadRelFromFile("Comment");

    Header date_header;
    date_header.add_sure(Attr("d", Type::DATE));
    Table date_table(date_header);
    Values row;
    row.push_back(Value(Type::DATE, "2009-03-04 17:41:05.915"));
    date_table.AddRow(row);
    CreateRel("Date", date_table);
    
    StringSet rel_names(GetRelNames());

    DeleteRels(StringSet());
    StringSet user_n_post;
    user_n_post.add_sure("User");
    user_n_post.add_sure("Post");
    BOOST_REQUIRE_THROW(DeleteRels(user_n_post), Error);
    BOOST_REQUIRE(GetRelNames() == rel_names);

    // Check Transactor throwing capabilities
        
    BadTransactor bad_transactor("Comment", "abracadabra");
    BOOST_REQUIRE_THROW(db.Perform(bad_transactor), Error);
    BOOST_REQUIRE(GetRelNames() == rel_names);
    
    ThrowingTransactor throwing_transactor(2, "Comment");
    db.Perform(throwing_transactor);
    BOOST_REQUIRE_EQUAL(throwing_transactor.GetCount(), 1);
}

////////////////////////////////////////////////////////////////////////////////
// Translator test
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class TestDBViewer : public DBViewer {
    public:
        TestDBViewer(DBFixture& db_fixture)
            : db_fixture_(db_fixture)
            , conn_("dbname=test_patsak password=1q2w3e") {}

        virtual const Header& GetRelHeader(const string& rel_name) const {
            headers_.push_back(Header());
            Header& header(headers_.back());
            const RichHeader&
                rich_header(db_fixture_.GetRelRichHeader(rel_name));
            header.reserve(rich_header.size());
            BOOST_FOREACH(const RichAttr& rich_attr, rich_header)
                header.add_sure(Attr(rich_attr.GetName(), rich_attr.GetType()));
            return header;
        }
        
        virtual string Quote(const PgLiter& pg_liter) const {
            return (pg_liter.quote_me
                    ? conn_.quote(pg_liter.str)
                    : pg_liter.str);
        }

        virtual RelFields GetReference(const RelFields& key) const {
            BOOST_FOREACH(const Constr& constr,
                          db_fixture_.GetRelConstrs(key.rel_name)) {
                const ForeignKey*
                    foreign_key_ptr = boost::get<ForeignKey>(&constr);
                if (foreign_key_ptr &&
                    foreign_key_ptr->key_field_names == key.field_names)
                    return RelFields(foreign_key_ptr->ref_rel_name,
                                     foreign_key_ptr->ref_field_names);
            }
            BOOST_FAIL("Reference was not found");
            throw 1; // NEVER REACHED
        }

    private:
        DBFixture& db_fixture_;
        mutable pqxx::connection conn_;
        mutable vector<Header> headers_;
    };
    
    
    class TranslateFunctor {
    public:
        TranslateFunctor(Translator& translator)
            : translator_(translator) {}

        string operator()(const string& ku_query) const {
            return translator_.TranslateQuery(TranslateItem(ku_query)).sql_str;
        }

    private:
        Translator& translator_;
    };
}


BOOST_FIXTURE_TEST_CASE(translator_test, DBFixture)
{
    LoadRelFromFile("User");
    LoadRelFromFile("Post");
    LoadRelFromFile("Comment");
    
    TestDBViewer db_viewer(*this);
    Translator translator(db_viewer);
    TranslateFunctor translate_functor(translator);
    TestFileStr("test/translator.test", translate_functor);

    BOOST_CHECK_THROW(translate_functor("User where Post.id"), Error);
    BOOST_CHECK_THROW(translate_functor("{smth: $0}"), Error);
    BOOST_CHECK_THROW(translate_functor("{smth: $1}"), Error);
    BOOST_CHECK_THROW(translate_functor("union(User, Post)"), Error);    
    BOOST_CHECK_THROW(translate_functor("{User.id, Post.id}"), Error);
    BOOST_CHECK_THROW(translate_functor("User where User[id, name]"), Error);
    //May be the following should work...
    BOOST_CHECK_THROW(translate_functor(
                          "for (x in Comment.author) x.author->name"),
                      Error);
    BOOST_CHECK_THROW(translate_functor("{n: User.name} where id % 2"), Error);
    BOOST_CHECK_THROW(translate_functor(
                          "{Post.text, Comment.text} where title == \"111\""),
                      Error);


    Types param_types;
    param_types.push_back(Type::STRING);
    param_types.push_back(Type::NUMBER);
    TranslateItem query_item("{name: $1, age: $2}", param_types);
    Translation trans(translator.TranslateQuery(query_item));
    BOOST_CHECK(
        WordComparator()(trans.sql_str,
                         "SELECT DISTINCT $1 AS \"name\", $2 AS \"age\""));
    Header header;
    header.add_sure(Attr("name", Type::STRING));
    header.add_sure(Attr("age", Type::NUMBER));
    BOOST_CHECK(trans.header == Header(header));

    Strings param_strs;
    param_strs.push_back("'anton'");
    param_strs.push_back("22");
    Translation
        str_trans(translator.TranslateQuery(
                      TranslateItem("{name: $1, age: $2}",
                                    param_types,
                                    0,
                                    param_strs)));
    BOOST_CHECK(
        WordComparator()(str_trans.sql_str,
                         "SELECT DISTINCT 'anton' AS \"name\", 22 AS \"age\""));

    Types spec_param_types;
    spec_param_types.push_back(Type::STRING);
    spec_param_types.push_back(Type::NUMBER);
    TranslateItems where_items;
    where_items.push_back(TranslateItem("name != $1 && age > $2",
                                        spec_param_types,
                                        2));
    Translation where_trans(translator.TranslateQuery(query_item, where_items));
    BOOST_CHECK(
        WordComparator()(where_trans.sql_str,
                         "SELECT * "
                         "FROM (SELECT DISTINCT $1 AS \"name\", $2 AS \"age\") "
                         "AS \"@\" "
                         "WHERE ((\"@\".\"name\" <> $3) "
                         "AND (\"@\".\"age\" > $4))"));

    TranslateItems by_items;
    by_items.push_back(TranslateItem("age"));
    Translation
        by_trans(translator.TranslateQuery(query_item,
                                           TranslateItems(),
                                           by_items));
    BOOST_CHECK(
        WordComparator()(by_trans.sql_str,
                         "SELECT * "
                         "FROM (SELECT DISTINCT $1 AS \"name\", $2 AS \"age\") "
                         "AS \"@\" "
                         "ORDER BY \"@\".\"age\""));

    StringSet fields;
    fields.add_sure("id");
    fields.add_sure("name");
    Translation
        only_trans(translator.TranslateQuery(TranslateItem("User"),
                                             TranslateItems(),
                                             TranslateItems(),
                                             &fields));
    BOOST_CHECK(
        WordComparator()(only_trans.sql_str,
                         "SELECT DISTINCT \"id\", \"name\" "
                         "FROM (SELECT DISTINCT \"User\".* FROM \"User\") "
                         "AS \"@\""));
    StringSet empty_string_set;
    Translation
        empty_only_trans(translator.TranslateQuery(TranslateItem("User"),
                                                   TranslateItems(),
                                                   TranslateItems(),
                                                   &empty_string_set));
    BOOST_CHECK(
        WordComparator()(empty_only_trans.sql_str,
                         "SELECT DISTINCT 1 "
                         "FROM (SELECT DISTINCT \"User\".* FROM \"User\") "
                         "AS \"@\""));

    StringSet only_name_fields;
    only_name_fields.add_sure("name");
    Translation ultimate_trans(translator.TranslateQuery(query_item,
                                                         where_items,
                                                         by_items,
                                                         &only_name_fields));
    BOOST_CHECK(
        WordComparator()(ultimate_trans.sql_str,
                         "SELECT DISTINCT ON (\"@\".\"age\") \"name\" "
                         "FROM (SELECT DISTINCT $1 AS \"name\", $2 AS \"age\") "
                         "AS \"@\" "
                         "WHERE ((\"@\".\"name\" <> $3) "
                         "AND (\"@\".\"age\" > $4)) "
                         "ORDER BY \"@\".\"age\""));

    TranslateItems delete_where_items;
    Types types1;
    types1.push_back(Type::STRING);
    delete_where_items.push_back(TranslateItem("name != $", types1));
    Types types2;
    types2.push_back(Type::NUMBER);
    delete_where_items.push_back(TranslateItem("age > $1", types2, 1));

    BOOST_CHECK(
        WordComparator()(translator.TranslateDelete("User", delete_where_items),
                         "DELETE FROM \"User\" "
                         "WHERE (\"User\".\"name\" <> $1) "
                         "AND (\"User\".\"age\" > $2)"));

    TranslateItems update_where_items(delete_where_items);
    StringMap field_expr_map;
    field_expr_map.insert(
        StringMap::value_type("flooder", "id == 0 || !flooder"));
    field_expr_map.insert(
        StringMap::value_type("name", "name + id + $"));
    Types types3;
    types3.push_back(Type::BOOLEAN);
    TranslateItem update_item("User", types3, 2);

    BOOST_CHECK(
        WordComparator()(translator.TranslateUpdate(update_item,
                                                    field_expr_map,
                                                    update_where_items),
                         "UPDATE \"User\" "
                         "SET \"flooder\" = ((\"User\".\"id\" = 0) "
                         "OR NOT \"User\".\"flooder\"), "
                         "\"name\" = ((\"User\".\"name\" || "
                         "ku.to_string(\"User\".\"id\")) || "
                         "ku.to_string($3)) "
                         "WHERE (\"User\".\"name\" <> $1) "
                         "AND (\"User\".\"age\" > $2)"));
}

////////////////////////////////////////////////////////////////////////////////
// Query tests
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class BadIndexGetter : public Transactor {
    public:
        BadIndexGetter(const string& query_str)
            : query_str_(query_str) {}
        
        virtual void operator()(Access& access) {
            QueryResult query_result(access.Query(query_str_,
                                                  Values(),
                                                  Specifiers()));
            auto_ptr<Values> values_ptr(
                query_result.GetValuesPtr(query_result.GetSize()));
            BOOST_CHECK(!values_ptr.get());
        }

    private:
        string query_str_;
    };
}


BOOST_FIXTURE_TEST_CASE(query_test, DBFixture)
{
    LoadRelFromFile("s");
    LoadRelFromFile("p");
    LoadRelFromFile("sp");

    FileTester<Table>("test/query.test",
                      bind(&DBFixture::Query, this, _1),
                      &ReadTableFromString)();

    BadIndexGetter bad_index_getter("s");
    db.Perform(bad_index_getter);

    LoadRelFromString("str", "val\nstring\n---\n'test\\\"");
    BOOST_CHECK(Query("str").GetValuesSet().at(0).at(0) ==
                Value(Type::STRING, "'test\\\""));

    LoadRelFromString("num", "val\nnumber\n---");
    Values cast_values;
    cast_values.push_back(Value(Type::STRING, "125.3"));
    InsertValues("num", cast_values);
    BOOST_CHECK(Query("num").GetValuesSet().at(0).at(0) ==
                Value(Type::NUMBER, 125.3));    

    CreateRel("empty", Table(Header()));
    InsertValues("empty", Values());
    BOOST_CHECK_THROW(InsertValues("empty", Values()), Error);
    BOOST_CHECK(Query("empty").GetValuesSet().size() == 1);

    LoadRelFromString("bool", "val\nboolean\n---\ntrue\nfalse");

    BOOST_CHECK_THROW(Query("sp[sid, pid]->sname"), Error);

    LoadRelFromString("s_p_ref",
                      "id\nnumber\nfk id - s - sid\nfk id - p - pid\n---");
    BOOST_CHECK_THROW(Query("s_p_ref.id->city"), Error);

    Specifiers specifiers;
    Values where_values;
    where_values.push_back(Value(Type::NUMBER, 25));
    where_values.push_back(Value(Type::STRING, "Blake"));
    specifiers.push_back(WhereSpecifier("status < $1 && sname != $2",
                                        where_values));
    StringSet only_fields1;
    only_fields1.add_sure("city");
    only_fields1.add_sure("sid");
    only_fields1.add_sure("sname");
    specifiers.push_back(OnlySpecifier(only_fields1));
    StringSet only_fields2;
    only_fields2.add_sure("sid");
    specifiers.push_back(OnlySpecifier(only_fields2));
    Values by_values;
    by_values.push_back(Value(Type::NUMBER, 2));
    by_values.push_back(Value(Type::NUMBER, 7));
    specifiers.push_back(BySpecifier("sid * $1 % $2", by_values));
    Values query_values;
    query_values.push_back(Value(Type::STRING, "Athens"));
    
    RichHeader sample_rich_header;
    sample_rich_header.add_sure(RichAttr("sid", Type::NUMBER));
    vector<Values> sample_values_list;
    Values sample_values;
    sample_values.push_back(Value(Type::NUMBER, 4));
    sample_values_list.push_back(sample_values);
    sample_values[0] = Value(Type::NUMBER, 1);
    sample_values_list.push_back(sample_values);
    sample_values[0] = Value(Type::NUMBER, 2);
    sample_values_list.push_back(sample_values);

    for (int i = 0; i < 20; ++i) {
        Table t(ComplexQuery("s where city != $", query_values, specifiers));
        BOOST_CHECK(t.GetRichHeader() == sample_rich_header);
        ValuesSet values_set(t.GetValuesSet());
        BOOST_CHECK(vector<Values>(values_set.begin(), values_set.end()) ==
                    sample_values_list);
    }
    
    specifiers.push_back(OnlySpecifier(StringSet()));
    ValuesSet one_empty_values;
    one_empty_values.add_sure(Values());
    BOOST_CHECK(ComplexQuery("s where city != $",
                             query_values,
                             specifiers).GetValuesSet() == one_empty_values);
}
