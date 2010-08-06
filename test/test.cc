// (c) 2008-2010 by Anton Korenyushkin

#include "../src/parser.h"
#include "../src/translator.h"
#include "../src/db.h"

#include <boost/test/unit_test.hpp>
#include <boost/function.hpp>
#include <boost/lexical_cast.hpp>

#include <fstream>

using namespace std;
using namespace ak;
using boost::function;
using boost::lexical_cast;


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
// Printers
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class Bracer {
    public:
        Bracer(ostream& os, const string& name)
            : os_(os) { os_ << '(' << name; }

        ~Bracer()     { os_ << ')'; }

    private:
        ostream& os_;
    };
}


namespace ak
{
    ostream& operator<<(ostream& os, const RangeVar& rv)
    {
        Bracer b(os, "rv");
        if (rv.GetName().empty())
            os << " *this*";
        else
            os << ' ' << rv.GetName()
               << ' ' << rv.GetRel();
        return os;
    }


    ostream& operator<<(ostream& os, const NamedExpr& ne)
    {
        Bracer b(os, "as");
        return os << ' ' << ne.name << ' ' << ne.expr;
    }


    ostream& operator<<(ostream& os, const Base& base)
    {
        Bracer b(os, "base");
        return os << ' ' << base.name;
    }


    ostream& operator<<(ostream& os, const Union& un)
    {
        Bracer b(os, "union");
        return os << ' ' << un.left << ' ' << un.right;
    }


    ostream& operator<<(ostream& os, const Select& select)
    {
        Bracer b(os, "where");
        os << ' ' << select.expr;
        BOOST_FOREACH(const Proto& proto, select.protos)
            os << ' ' << proto;
        return os;
    }


    ostream& operator<<(ostream& os, const Liter& liter)
    {
        return os << liter.value;
    }


    void PrintStringSet(ostream& os, const StringSet& ss)
    {
        BOOST_FOREACH(const string& str, ss)
            os << ' ' << str;
    }


    void PrintKeys(ostream& os, const MultiField& multi_field, size_t n)
    {
        os << ' ';
        if (n) {
            Bracer b(os, "key");
            PrintStringSet(os, multi_field.path[n - 1]);
            PrintKeys(os, multi_field, n - 1);
        } else
            os << multi_field.rv;
    }


    ostream& operator<<(ostream& os, const MultiField& multi_field)
    {
        Bracer b(os, multi_field.IsMulti() ? "fields" : "field");
        PrintStringSet(os, multi_field.path.back());
        PrintKeys(os, multi_field, multi_field.path.size() - 1);
        return os;
    }


    ostream& operator<<(ostream& os, const PosArg& pos_arg)
    {
        return os << '$' << pos_arg.pos;
    }


    ostream& operator<<(ostream& os, const Quant& quant)
    {
        Bracer b(os, quant.flag ? "always" : "once");
        os << ' ' << quant.pred;
        BOOST_FOREACH(const RangeVar& rv, quant.rv_set)
            os << ' ' << rv;
        return os;
    }


    ostream& operator<<(ostream& os, const Binary& binary)
    {
        Bracer b(os, binary.op.GetName());
        return os << ' ' << binary.left << ' ' << binary.right;
    }


    ostream& operator<<(ostream& os, const Unary& unary)
    {
        Bracer b(os, unary.op.GetName());
        return os << ' ' << unary.operand;
    }


    ostream& operator<<(ostream& os, const Cond& cond)
    {
        Bracer b(os, "?");
        return os << ' ' << cond.term << ' ' << cond.yes << ' ' << cond.no;
    }
}

////////////////////////////////////////////////////////////////////////////////
// orset test
////////////////////////////////////////////////////////////////////////////////

BOOST_AUTO_TEST_CASE(orset_test)
{
    orset<int> a;
    a.add_safely(1);
    a.add_safely(2);
    a.add_safely(3);
    a.add_safely(2);
    a.add_safely(1);
    a.add_safely(4);
    vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.push_back(4);
    BOOST_CHECK(vector<int>(a.begin(), a.end()) == v);
    orset<int> b;
    b.add(4);
    b.add(3);
    b.add(2);
    b.add(1);
    BOOST_CHECK(a == b);
    b.add(5);
    BOOST_CHECK(a != b);
    BOOST_CHECK(a.find(3));
    BOOST_CHECK(!a.find(10));
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

namespace ak
{
    bool operator==(const Value& lhs, const Value& rhs)
    {
        if (lhs.GetType() != rhs.GetType())
            return false;
        double ld = 0, rd = 0;
        string ls, rs;
        return lhs.Get(ld, ls) == rhs.Get(rd, rs) && ld == rd && ls == rs;
    }


    bool operator==(const DefAttr& lhs, const DefAttr& rhs)
    {
        if (lhs.name != rhs.name || lhs.type != rhs.type)
            return false;
        if (!lhs.default_ptr && !rhs.default_ptr)
            return true;
        if (!lhs.default_ptr || !rhs.default_ptr)
            return false;
        return *lhs.default_ptr == *rhs.default_ptr;
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
        Table(const DefHeader& def_header,
              const UniqueKeySet& unique_key_set = UniqueKeySet(),
              const ForeignKeySet& foreign_key_set = ForeignKeySet());
        Table(const Header& header);

        Table(istream& is);
        bool operator==(const Table& other) const;
        bool operator!=(const Table& other) const { return !(*this == other); }
        friend ostream& operator<<(ostream& os, const Table& table);
        const ValuesSet& GetValuesSet() const;
        const DefHeader& GetDefHeader() const;
        const UniqueKeySet& GetUniqueKeySet() const;
        const ForeignKeySet& GetForeignKeySet() const;
        const Strings& GetChecks() const;

        void SetDefHeader(const DefHeader& def_header);
        void SetUniqueKeys(const UniqueKeySet& unique_key_set);
        void SetChecks(const Strings& checks);
        void SetForeignKeySet(const ForeignKeySet& foreign_key_set);
        void AddRow(const Values& values);

    private:
        DefHeader def_header_;
        UniqueKeySet unique_key_set_;
        ForeignKeySet foreign_key_set_;
        Strings checks_;
        ValuesSet values_set_;

        void ReadHeader(istream& is);
        static Value ReadValue(istream& is, Type type);
        void ReadMetaData(istream& is);
        void ReadValuesSet(istream& is);
        void PrintHeader(ostream& os) const;
        void PrintValuesSet(ostream& os) const;
        void AddAllUniqueKey();
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


Table::Table(const DefHeader& def_header,
             const UniqueKeySet& unique_key_set,
             const ForeignKeySet& foreign_key_set)
    : def_header_(def_header)
    , unique_key_set_(unique_key_set)
    , foreign_key_set_(foreign_key_set)
{
    AddAllUniqueKey();
}


Table::Table(const Header& header)
{
    def_header_.reserve(header.size());
    BOOST_FOREACH(const Attr& attr, header)
        def_header_.add(DefAttr(attr.name, attr.type));
    AddAllUniqueKey();
}


Table::Table(istream& is)
{
    ReadHeader(is);
    ReadMetaData(is);
    AddAllUniqueKey();
    ReadValuesSet(is);
}


bool Table::operator==(const Table& other) const
{
    return (def_header_ == other.def_header_ &&
            unique_key_set_ == other.unique_key_set_ &&
            foreign_key_set_ == other.foreign_key_set_ &&
            values_set_ == other.values_set_);
}


const ValuesSet& Table::GetValuesSet() const
{
    return values_set_;
}


const DefHeader& Table::GetDefHeader() const
{
    return def_header_;
}


const UniqueKeySet& Table::GetUniqueKeySet() const
{
    return unique_key_set_;
}


const ForeignKeySet& Table::GetForeignKeySet() const
{
    return foreign_key_set_;
}


const Strings& Table::GetChecks() const
{
    return checks_;
}


void Table::SetDefHeader(const DefHeader& def_header)
{
    def_header_ = def_header;
}


void Table::SetUniqueKeys(const UniqueKeySet& unique_key_set)
{
    unique_key_set_ = unique_key_set;
}


void Table::SetForeignKeySet(const ForeignKeySet& foreign_key_set)
{
    foreign_key_set_ = foreign_key_set;
}


void Table::SetChecks(const Strings& checks)
{
    checks_ = checks;
}


void Table::AddRow(const Values& values)
{
    values_set_.add(values);
}


void Table::ReadHeader(istream& is)
{
    string names_line, types_line;
    getline(is, names_line);
    getline(is, types_line);
    BOOST_CHECK(!names_line.empty() && ! types_line.empty());
    istringstream names_iss(names_line), types_iss(types_line);
    for (;;) {
        string name, type_str;
        names_iss >> name;
        types_iss >> type_str;
        if (name.empty() && type_str.empty())
            break;
        BOOST_CHECK(!name.empty() && ! type_str.empty());
        Type type;
        if (type_str == "number") {
            type = Type::NUMBER;
        } else if (type_str == "int") {
            type = Type::INTEGER;
        } else if (type_str == "serial") {
            type = Type::SERIAL;
        } else if (type_str == "string") {
            type = Type::STRING;
        } else if (type_str == "bool") {
            type = Type::BOOLEAN;
        } else if (type_str == "date") {
            type = Type::DATE;
        } else if (type_str == "json") {
            type = Type::JSON;
        } else {
            AK_ASSERT_EQUAL(type_str, "binary");
            type = Type::BINARY;
        }
        def_header_.add(DefAttr(name, type));
    }
}


Value Table::ReadValue(istream& is, Type type)
{
    if (type.IsNumeric()) {
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
            checks_.push_back(line.substr(string("check").size() + 1));
        } else if (constr_name == "fk") {
            StringSet key_attr_names;
            for (;;) {
                string name;
                line_iss >> name;
                BOOST_REQUIRE(!line_iss.eof());
                if (name == "-")
                    break;
                key_attr_names.add(name);
            }
            string ref_rel_var_name;
            line_iss >> ref_rel_var_name;
            string delimiter;
            line_iss >> delimiter;
            BOOST_REQUIRE(delimiter == "-");
            StringSet ref_attr_names;
            for (;;) {
                string name;
                line_iss >> name;
                if (name.empty())
                    break;
                ref_attr_names.add(name);
            }
            foreign_key_set_.add(
                ForeignKey(key_attr_names, ref_rel_var_name, ref_attr_names));
        } else if (constr_name == "unique") {
            StringSet unique_key;
            for (;;) {
                string name;
                line_iss >> name;
                if (name.empty())
                    break;
                unique_key.add(name);
            }
            unique_key_set_.add(unique_key);
        } else if (constr_name == "default") {
            string field_name;
            line_iss >> field_name;
            const DefAttr& def_attr(GetAttr(def_header_, field_name));
            Value value(ReadValue(line_iss, def_attr.type));
            const_cast<DefAttr&>(def_attr).default_ptr = value;
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
        values.reserve(def_header_.size());
        BOOST_FOREACH(const DefAttr& def_attr, def_header_)
            values.push_back(ReadValue(iss, def_attr.type));
        values_set_.add(values);
    }
}


void Table::PrintHeader(ostream& os) const
{
    BOOST_FOREACH(const DefAttr& def_attr, def_header_)
        os << def_attr.name << ' ';
    os << '\n';
    BOOST_FOREACH(const DefAttr& def_attr, def_header_)
        os << def_attr.type.GetName() << ' ';
    os << '\n';
}


void Table::PrintValuesSet(ostream& os) const
{
    BOOST_FOREACH(const Values& values, values_set_) {
        BOOST_FOREACH(const Value& value, values)
            os << value << ' ';
        os << '\n';
    }
}


void Table::AddAllUniqueKey()
{
    if (def_header_.empty())
        return;
    StringSet all_unique_key;
    all_unique_key.reserve(def_header_.size());
    BOOST_FOREACH(const DefAttr& def_attr, def_header_)
        all_unique_key.add(def_attr.name);
    unique_key_set_.add_safely(all_unique_key);
}

////////////////////////////////////////////////////////////////////////////////
// Draft definitions
////////////////////////////////////////////////////////////////////////////////

struct Draft::Impl {
    Value value;

    Impl(const Value& value) : value(value) {}
};


Draft::Draft(Impl* pimpl)
    : pimpl_(pimpl)
{
}


Draft::~Draft()
{
}


Value Draft::Get(Type /*type*/) const
{
    return pimpl_->value;
}


namespace
{
    Draft CreateDraft(const Value& value)
    {
        return Draft(new Draft::Impl(value));
    }
}

////////////////////////////////////////////////////////////////////////////////
// DB test
////////////////////////////////////////////////////////////////////////////////

namespace
{
    Table DumpRel(const string& query)
    {
        Header header;
        vector<Values> tuples;
        Query(header, tuples, query);
        Table result(header);
        BOOST_FOREACH(const Values& values, tuples)
            result.AddRow(values);
        return result;
    }


    Table DumpRelVar(const string& rel_var_name)
    {
        Table result(DumpRel(rel_var_name));
        result.SetDefHeader(GetDefHeader(rel_var_name));
        result.SetUniqueKeys(GetUniqueKeySet(rel_var_name));
        result.SetForeignKeySet(GetForeignKeySet(rel_var_name));
        return result;
    }


    void LoadRelVar(const string& rel_var_name, const Table& table)
    {
        const DefHeader& def_header(table.GetDefHeader());
        CreateRelVar(rel_var_name,
                     def_header,
                     table.GetUniqueKeySet(),
                     table.GetForeignKeySet(),
                     table.GetChecks());
        BOOST_FOREACH(const Values& values, table.GetValuesSet()) {
            assert(values.size() == def_header.size());
            DraftMap draft_map;
            for (size_t i = 0; i < def_header.size(); ++i)
                draft_map.add(
                    NamedDraft(def_header[i].name, CreateDraft(values[i])));
            Insert(rel_var_name, draft_map);
        }
        Commit();
        BOOST_CHECK(DumpRelVar(rel_var_name) == table);
    }


    void LoadRelVarFromFile(const string& rel_var_name)
    {
        LoadRelVar(rel_var_name,
                   ReadTableFromFile("test/" + rel_var_name + ".table"));
    }


    void LoadRelVarFromString(const string& rel_var_name, const string& str)
    {
        LoadRelVar(rel_var_name, ReadTableFromString(str));
    }
}


BOOST_AUTO_TEST_CASE(db_test)
{
    DropRelVars(GetRelVarNames());
    LoadRelVarFromFile("User");
    LoadRelVarFromFile("Post");
    LoadRelVarFromFile("Comment");

    Header header;
    header.add(Attr("d", Type::DATE));
    header.add(Attr("j", Type::JSON));
    header.add(Attr("b", Type::BINARY));
    Table table(header);
    Values row;
    row.push_back(Value(Type::DATE, "2009-03-04 17:41:05.915"));
    row.push_back(Value(Type::JSON, "{}"));
    row.push_back(Value(Type::BINARY, ""));
    table.AddRow(row);
    LoadRelVar("Test", table);

    StringSet rel_var_names(GetRelVarNames());

    DropRelVars(StringSet());
    StringSet user_and_post;
    user_and_post.add("User");
    user_and_post.add("Post");
    BOOST_REQUIRE_THROW(DropRelVars(user_and_post), Error);

    Commit();
    DropRelVars(GetRelVarNames());
    RollBack();
    BOOST_REQUIRE(GetRelVarNames() == rel_var_names);
}

////////////////////////////////////////////////////////////////////////////////
// Translator test
////////////////////////////////////////////////////////////////////////////////

namespace
{
    string DoTranslateQuery(const string& query)
    {
        Header header;
        return TranslateQuery(header, query);
    }
}


BOOST_AUTO_TEST_CASE(translator_test)
{
    DropRelVars(GetRelVarNames());
    LoadRelVarFromFile("User");
    LoadRelVarFromFile("Post");
    LoadRelVarFromFile("Comment");

    TestFileStr("test/translator.test", DoTranslateQuery);

    BOOST_CHECK_THROW(DoTranslateQuery("User where Post.id"), Error);
    BOOST_CHECK_THROW(DoTranslateQuery("{smth: $0}"), Error);
    BOOST_CHECK_THROW(DoTranslateQuery("{smth: $1}"), Error);
    BOOST_CHECK_THROW(DoTranslateQuery("union(User, Post)"), Error);
    BOOST_CHECK_THROW(DoTranslateQuery("union(User.id, User.name)"), Error);
    BOOST_CHECK_THROW(DoTranslateQuery("{User.id, Post.id}"), Error);
    BOOST_CHECK_THROW(DoTranslateQuery("User where User[id, name]"), Error);
    //May be the following should work...
    BOOST_CHECK_THROW(DoTranslateQuery(
                          "for (x in Comment.author) x.author->name"),
                      Error);
    BOOST_CHECK_THROW(DoTranslateQuery("{n: User.name} where id % 2"), Error);
    BOOST_CHECK_THROW(DoTranslateQuery(
                          "{Post.text, Comment.text} where title == \"111\""),
                      Error);


    Drafts params;
    params.push_back(CreateDraft(Value(Type::STRING, "anton")));
    params.push_back(CreateDraft(Value(Type::NUMBER, 23)));
    Header header;
    BOOST_CHECK_EQUAL(
        TranslateQuery(header, "{name: $1, age: $2}", params),
        "SELECT DISTINCT 'anton' AS \"name\", 23 AS \"age\"");
    BOOST_CHECK_EQUAL(header.size(), 2);
    BOOST_CHECK_EQUAL(header[0].name, "name");
    BOOST_CHECK(header[0].type == Type::STRING);
    BOOST_CHECK_EQUAL(header[1].name, "age");
    BOOST_CHECK(header[1].type == Type::NUMBER);

    Strings by_exprs;
    by_exprs.push_back("id % $1");
    by_exprs.push_back("name + $2");
    Drafts by_params;
    by_params.push_back(CreateDraft(Value(Type::NUMBER, 42)));
    by_params.push_back(CreateDraft(Value(Type::STRING, "abc")));
    BOOST_CHECK_EQUAL(
        TranslateQuery(header, "User", Drafts(), by_exprs, by_params, 3, 4),
        "SELECT * FROM (SELECT DISTINCT \"User\".* FROM \"User\") AS \"@\" "
        "ORDER BY (\"@\".\"id\" % 42), (\"@\".\"name\" || 'abc') "
        "LIMIT 4 OFFSET 3");
    BOOST_CHECK_EQUAL(
        TranslateQuery(header, "User", Drafts(), Strings(), Drafts(), 5),
        "SELECT DISTINCT \"User\".* FROM \"User\" OFFSET 5");
    BOOST_CHECK_EQUAL(
        TranslateQuery(header, "User", Drafts(), Strings(), Drafts(), 0, 6),
        "SELECT DISTINCT \"User\".* FROM \"User\" LIMIT 6");

    params.clear();
    params.push_back(CreateDraft(Value(Type::NUMBER, 2)));
    BOOST_CHECK_EQUAL(
        TranslateCount("User where id % $ == 0", params),
        "SELECT COUNT(*) FROM ("
        "SELECT DISTINCT \"User\".* "
        "FROM \"User\" "
        "WHERE ((\"User\".\"id\" % 2) = 0)) AS \"@\"");

    BOOST_CHECK_EQUAL(
        TranslateDelete("User","id % $ == 0", params),
        "DELETE FROM \"User\" WHERE ((\"User\".\"id\" % 2) = 0)");

    StringMap expr_map;
    expr_map.add(NamedString("flooder", "id == 0 || !flooder"));
    expr_map.add(NamedString("name", "name + id + $"));
    Drafts expr_params;
    expr_params.push_back(CreateDraft(Value(Type::STRING, "abc")));
    BOOST_CHECK_EQUAL(
        TranslateUpdate("User", "id % $ == 0", params, expr_map, expr_params),
        "UPDATE \"User\" SET "
        "\"flooder\" = "
        "((\"User\".\"id\" = 0) OR NOT \"User\".\"flooder\"), "
        "\"name\" = "
        "((\"User\".\"name\" || ak.to_string(\"User\".\"id\")) || 'abc') "
        "WHERE ((\"User\".\"id\" % 2) = 0)");
}

////////////////////////////////////////////////////////////////////////////////
// Query tests
////////////////////////////////////////////////////////////////////////////////

BOOST_AUTO_TEST_CASE(query_test)
{
    DropRelVars(GetRelVarNames());
    LoadRelVarFromFile("s");
    LoadRelVarFromFile("p");
    LoadRelVarFromFile("sp");

    FileTester<Table>("test/query.test", &DumpRel, &ReadTableFromString)();

    LoadRelVarFromString("str", "val\nstring\n---\n'test\\\"");
    BOOST_CHECK(DumpRel("str").GetValuesSet()[0][0] ==
                Value(Type::STRING, "'test\\\""));

    LoadRelVarFromString("num", "val\nnumber\n---\n125.3");
    BOOST_CHECK(DumpRel("num").GetValuesSet()[0][0] ==
                Value(Type::NUMBER, 125.3));

    LoadRelVarFromString("bool", "val\nbool\n---\ntrue\nfalse");

    BOOST_CHECK_THROW(DumpRel("sp[sid, pid]->sname"), Error);

    LoadRelVarFromString("s_p_ref",
                         "id\nnumber\nfk id - s - sid\nfk id - p - pid\n---");
    BOOST_CHECK_THROW(DumpRel("s_p_ref.id->city"), Error);
}

////////////////////////////////////////////////////////////////////////////////
// init_unit_test_suite
////////////////////////////////////////////////////////////////////////////////

boost::unit_test::test_suite* init_unit_test_suite(int, char**)
{
    InitDatabase("dbname=test-patsak", "public", "pg_default");
    return 0;
}
