
// (c) 2008-2010 by Anton Korenyushkin

#define BOOST_TEST_MAIN


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

#include <iostream>
#include <fstream>

using namespace std;
using namespace ku;
using boost::bind;
using boost::function;
using boost::lexical_cast;
using boost::noncopyable;
using boost::static_visitor;
using boost::apply_visitor;


////////////////////////////////////////////////////////////////////////////////
// TestReader
////////////////////////////////////////////////////////////////////////////////

namespace
{
    // Class for reading test pairs from a file
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
    // Class transforming parts of test pairs into type T
    // and comparing results
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
    // String null transformation
    string NullTransform(const string& str)
    {
        return str;
    }
    

    // Word-by-word string comparison
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


    // Functor adaptor (returns printed result)
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


    // StrFunctor instantiator
    template <typename Func>
    StrFunctor<Func> StrFunc(Func f)
    {
        return StrFunctor<Func>(f);
    }


    // Routine for common case of string based file tests
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

namespace
{
    class ConstrCompareVisitor : public static_visitor<bool> {
    public:
        template <typename T, typename U>
        bool operator()(const T& /*lhs*/, const U& /*rhs*/) const {
            return false;
        }
        
        bool operator()(const Unique& lhs, const Unique& rhs) const {
            return lhs.field_names == rhs.field_names;
        }

        bool operator()(const ForeignKey& lhs, const ForeignKey& rhs) const {
            return (lhs.key_field_names  == rhs.key_field_names &&
                    lhs.ref_rel_var_name == rhs.ref_rel_var_name &&
                    lhs.ref_field_names  == rhs.ref_field_names);
        }
    
        bool operator()(const Check& lhs, const Check& rhs) const {
            return lhs.expr_str == rhs.expr_str;
        }
    };

    
    class ConstrComparator : public binary_function<Constr, Constr, bool> {
    public:
        bool operator()(const Constr& lhs, const Constr& rhs) const {
            return apply_visitor(ConstrCompareVisitor(), lhs, rhs);
        }
    };
}


namespace ku
{
    bool operator==(const Constrs& lhs, const Constrs& rhs)
    {
        return (
            orset<Constr, ConstrComparator>(lhs.begin(), lhs.end(), false) ==
            orset<Constr, ConstrComparator>(rhs.begin(), rhs.end(), false));
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
        
    // In-memory relation representation
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
    } else if (type == Type::BOOL) {
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
            string ref_rel_var_name;
            line_iss >> ref_rel_var_name;
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
                                          ref_rel_var_name,
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
                                 ? Type::INTEGER
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
    class DBFixture : private noncopyable {
    public:
        DB db;
        
        DBFixture();
        void LoadRelVarFromFile(const string& rel_var_name);
        void LoadRelVarFromString(const string& rel_var_name,
                                  const string& str);
        Table Query(const string& query);
        Table DumpRelVar(const string& rel_var_name);
        void InsertValues(const string& rel_var_name, const Values& values);
        void CreateRelVar(const string& rel_var_name, const Table& table);
        StringSet GetRelVarNames();
        const RichHeader& GetRelVarRichHeader(const string& rel_var_name);
        void DeleteRelVars(const StringSet& rel_var_names);
        const Constrs& GetRelVarConstrs(const string& rel_var_name);
    };
}


DBFixture::DBFixture()
    : db("dbname=test_patsak user=test password=test", "test", "test-app")
{
    DeleteRelVars(GetRelVarNames());
    BOOST_REQUIRE(GetRelVarNames().empty());
}


void DBFixture::LoadRelVarFromFile(const string& rel_var_name)
{
    CreateRelVar(rel_var_name,
                 ReadTableFromFile("test/" + rel_var_name + ".table"));
}


void DBFixture::LoadRelVarFromString(const string& rel_var_name,
                                     const string& str)
{
    CreateRelVar(rel_var_name, ReadTableFromString(str));
}


Table DBFixture::Query(const string& query)
{
    Access access(db);
    QueryResult query_result(access.Query(query));
    Table result(query_result.header);
    BOOST_FOREACH(const Values& values, query_result.tuples)
        result.AddRow(values);
    return result;
}


Table DBFixture::DumpRelVar(const string& rel_var_name)
{
    Table result(Query(rel_var_name));
    Access access(db);
    result.SetRichHeader(access.GetRichHeader(rel_var_name));
    result.SetConstrs(access.GetConstrs(rel_var_name));
    return result;
}


void DBFixture::InsertValues(const string& rel_var_name, const Values& values)
{
    Access access(db);
    const RichHeader& rich_header(access.GetRichHeader(rel_var_name));
    assert(values.size() == rich_header.size());
    ValueMap value_map;
    for (size_t i = 0; i < rich_header.size(); ++i)
        value_map.insert(ValueMap::value_type(rich_header[i].GetName(),
                                              values[i]));
    access.Insert(rel_var_name, value_map);
    access.Commit();
}


void DBFixture::CreateRelVar(const string& rel_var_name, const Table& table)
{
    Access access(db);
    const RichHeader& rich_header(table.GetRichHeader());
    access.Create(rel_var_name, rich_header, table.GetConstrs());
    BOOST_FOREACH(const Values& values, table.GetValuesSet()) {
        assert(values.size() == rich_header.size());
        ValueMap value_map;
        for (size_t i = 0; i < rich_header.size(); ++i)
            value_map.insert(
                ValueMap::value_type(rich_header[i].GetName(),
                                     values[i]));
        access.Insert(rel_var_name, value_map);
    }
    access.Commit();
    BOOST_CHECK(DumpRelVar(rel_var_name) == table);
}


StringSet DBFixture::GetRelVarNames()
{
    Access access(db);
    return access.GetNames();
}


const RichHeader& DBFixture::GetRelVarRichHeader(const string& rel_var_name)
{
    Access access(db);
    return access.GetRichHeader(rel_var_name);
}


void DBFixture::DeleteRelVars(const StringSet& rel_var_names)
{
    Access access(db);
    access.Drop(rel_var_names);
    access.Commit();
}


const Constrs& DBFixture::GetRelVarConstrs(const string& rel_var_name)
{
    Access access(db);
    return access.GetConstrs(rel_var_name);
}

////////////////////////////////////////////////////////////////////////////////
// DB test
////////////////////////////////////////////////////////////////////////////////

BOOST_FIXTURE_TEST_CASE(db_test, DBFixture)
{
    LoadRelVarFromFile("User");
    LoadRelVarFromFile("Post");
    LoadRelVarFromFile("Comment");

    Header date_header;
    date_header.add_sure(Attr("d", Type::DATE));
    Table date_table(date_header);
    Values row;
    row.push_back(Value(Type::DATE, "2009-03-04 17:41:05.915"));
    date_table.AddRow(row);
    CreateRelVar("Date", date_table);
    
    StringSet rel_var_names(GetRelVarNames());

    DeleteRelVars(StringSet());
    StringSet user_n_post;
    user_n_post.add_sure("User");
    user_n_post.add_sure("Post");
    BOOST_REQUIRE_THROW(DeleteRelVars(user_n_post), Error);
    BOOST_REQUIRE(GetRelVarNames() == rel_var_names);
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
            , conn_("dbname=test_patsak user=test password=test") {}

        virtual const Header& GetHeader(const string& rel_var_name) const {
            headers_.push_back(Header());
            Header& header(headers_.back());
            const RichHeader&
                rich_header(db_fixture_.GetRelVarRichHeader(rel_var_name));
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

        virtual RelVarFields GetReference(const RelVarFields& key) const {
            BOOST_FOREACH(const Constr& constr,
                          db_fixture_.GetRelVarConstrs(key.rel_var_name)) {
                const ForeignKey*
                    foreign_key_ptr = boost::get<ForeignKey>(&constr);
                if (foreign_key_ptr &&
                    foreign_key_ptr->key_field_names == key.field_names)
                    return RelVarFields(foreign_key_ptr->ref_rel_var_name,
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

        string operator()(const string& query) const {
            Header header;
            return translator_.TranslateQuery(header, query);
        }

    private:
        Translator& translator_;
    };
}


BOOST_FIXTURE_TEST_CASE(translator_test, DBFixture)
{
    LoadRelVarFromFile("User");
    LoadRelVarFromFile("Post");
    LoadRelVarFromFile("Comment");
    
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


    Values params;
    params.push_back(Value(Type::STRING, "anton"));
    params.push_back(Value(Type::NUMBER, 23));
    Header header;
    BOOST_CHECK_EQUAL(
        translator.TranslateQuery(header, "{name: $1, age: $2}", params),
        "SELECT DISTINCT 'anton' AS \"name\", 23 AS \"age\"");
    BOOST_CHECK_EQUAL(
        lexical_cast<string>(header),
        "{\"name\": \"string\", \"age\": \"number\"}");

    Strings by_exprs;
    by_exprs.push_back("id % $1");
    by_exprs.push_back("name + $2");
    Values by_params;
    by_params.push_back(Value(Type::NUMBER, 42));
    by_params.push_back(Value(Type::STRING, "abc"));
    BOOST_CHECK_EQUAL(
        translator.TranslateQuery(header, "User", Values(),
                                  by_exprs, by_params, 3, 4),
        "SELECT * FROM (SELECT DISTINCT \"User\".* FROM \"User\") AS \"@\" "
        "ORDER BY (\"@\".\"id\" % 42), (\"@\".\"name\" || 'abc') "
        "LIMIT 4 OFFSET 3");
    BOOST_CHECK_EQUAL(
        translator.TranslateQuery(header, "User", Values(),
                                  Strings(), Values(), 5),
        "SELECT DISTINCT \"User\".* FROM \"User\" OFFSET 5");
    BOOST_CHECK_EQUAL(
        translator.TranslateQuery(header, "User", Values(),
                                  Strings(), Values(), 0, 6),
        "SELECT DISTINCT \"User\".* FROM \"User\" LIMIT 6");

    params.clear();
    params.push_back(Value(Type::NUMBER, 2));
    BOOST_CHECK_EQUAL(
        translator.TranslateCount("User where id % $ == 0", params),
        "SELECT COUNT(*) FROM ("
        "SELECT DISTINCT \"User\".* "
        "FROM \"User\" "
        "WHERE ((\"User\".\"id\" % 2) = 0)) AS \"@\"");

    BOOST_CHECK_EQUAL(
        translator.TranslateDelete("User","id % $ == 0", params),
        "DELETE FROM \"User\" WHERE ((\"User\".\"id\" % 2) = 0)");
    
    StringMap field_expr_map;
    field_expr_map.insert(
        StringMap::value_type("flooder", "id == 0 || !flooder"));
    field_expr_map.insert(
        StringMap::value_type("name", "name + id + $"));
    Values expr_params;
    expr_params.push_back(Value(Type::STRING, "abc"));
    BOOST_CHECK_EQUAL(
        translator.TranslateUpdate(
            "User", "id % $ == 0", params, field_expr_map, expr_params),
        "UPDATE \"User\" SET "
        "\"flooder\" = "
        "((\"User\".\"id\" = 0) OR NOT \"User\".\"flooder\"), "
        "\"name\" = "
        "((\"User\".\"name\" || ku.to_string(\"User\".\"id\")) || 'abc') "
        "WHERE ((\"User\".\"id\" % 2) = 0)");
}

////////////////////////////////////////////////////////////////////////////////
// Query tests
////////////////////////////////////////////////////////////////////////////////

BOOST_FIXTURE_TEST_CASE(query_test, DBFixture)
{
    LoadRelVarFromFile("s");
    LoadRelVarFromFile("p");
    LoadRelVarFromFile("sp");

    FileTester<Table>("test/query.test",
                      bind(&DBFixture::Query, this, _1),
                      &ReadTableFromString)();

    LoadRelVarFromString("str", "val\nstring\n---\n'test\\\"");
    BOOST_CHECK(Query("str").GetValuesSet().at(0).at(0) ==
                Value(Type::STRING, "'test\\\""));

    LoadRelVarFromString("num", "val\nnumber\n---");
    Values cast_values;
    cast_values.push_back(Value(Type::STRING, "125.3"));
    InsertValues("num", cast_values);
    BOOST_CHECK(Query("num").GetValuesSet().at(0).at(0) ==
                Value(Type::NUMBER, 125.3));    

    CreateRelVar("empty", Table(Header()));
    InsertValues("empty", Values());
    BOOST_CHECK_THROW(InsertValues("empty", Values()), Error);
    BOOST_CHECK(Query("empty").GetValuesSet().size() == 1);

    LoadRelVarFromString("bool", "val\nbool\n---\ntrue\nfalse");

    BOOST_CHECK_THROW(Query("sp[sid, pid]->sname"), Error);

    LoadRelVarFromString("s_p_ref",
                         "id\nnumber\nfk id - s - sid\nfk id - p - pid\n---");
    BOOST_CHECK_THROW(Query("s_p_ref.id->city"), Error);
}
