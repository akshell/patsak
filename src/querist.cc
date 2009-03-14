
// (c) 2008 by Anton Korenyushkin

/// \file querist.cc
/// Database query impl


#include "querist.h"
#include "translator.h"
#include "utils.h"

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/operators.hpp>
#include <boost/utility.hpp>

#include <map>

using namespace ku;
using namespace std;
using boost::static_visitor;
using boost::apply_visitor;
using boost::lexical_cast;
using boost::noncopyable;


////////////////////////////////////////////////////////////////////////////////
// Empiric constants
////////////////////////////////////////////////////////////////////////////////

namespace
{
#ifdef TEST
    const size_t PREPARATION_USE_COUNT = 2;
    const size_t PREPARED_MAP_CLEAR_SIZE = 3;
    const size_t USE_COUNT_MAP_CLEAR_SIZE = 15;
#else
    const size_t PREPARATION_USE_COUNT = 5;
    const size_t PREPARED_MAP_CLEAR_SIZE = 500;
    const size_t USE_COUNT_MAP_CLEAR_SIZE = 1000;
#endif
}

////////////////////////////////////////////////////////////////////////////////
// QueryResult definitions
////////////////////////////////////////////////////////////////////////////////

struct QueryResult::Data : private noncopyable {
    Header header;
    pqxx::result result;

    Data(const Header& header, const pqxx::result& result)
        : header(header), result(result) {}
};


QueryResult::QueryResult(Data* data_ptr)
    : data_ptr_(data_ptr)
{
}


QueryResult::~QueryResult()
{
}


size_t QueryResult::GetSize() const
{
    if (data_ptr_->header.empty())
        return data_ptr_->result.empty() ? 0 : 1;
    return data_ptr_->result.size();
}


Values QueryResult::GetValues(size_t idx) const
{
    if (idx >= GetSize())
        throw Error("Values index out of bounds");
    if (data_ptr_->header.empty()) {
        KU_ASSERT(GetSize() == 1);
        return Values();
    }
    pqxx::result::tuple pqxx_tuple(data_ptr_->result[idx]);
    size_t tuple_size = pqxx_tuple.size();
    KU_ASSERT(tuple_size == GetHeader().size());
    Values result;
    result.reserve(tuple_size);
    for (size_t idx = 0; idx < tuple_size; ++idx) {
        pqxx::result::field field(pqxx_tuple[idx]);
        KU_ASSERT(!field.is_null());
        Type type(GetHeader()[idx].GetType());
        if (type == Type::NUMBER) {
            double d;
            field >> d;
            result.push_back(Value(type, d));
        } else if (type == Type::STRING) {
            result.push_back(Value(type, field.c_str()));        
        } else if (type == Type::BOOLEAN) {
            bool b;
            field >> b;
            result.push_back(Value(type, b));
        } else {
            KU_ASSERT(type == Type::DATE);
            result.push_back(Value(type, field.c_str()));
        }
    }
    return result;
}


const Header& QueryResult::GetHeader() const
{
    return data_ptr_->header;
}

////////////////////////////////////////////////////////////////////////////////
// QueryFamily
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// The family of queries (without specifiers) which could be made
    /// through one prepared statement
    class QueryFamily : private
    boost::less_than_comparable<
        QueryFamily,
        boost::equality_comparable<QueryFamily> > {
    public:
        enum Sort {
            QUERY,
            UPDATE,
            DELETE
        };
        
        QueryFamily(Sort sort,
                    const string& ku_str,
                    const Types& param_types = Types(),
                    const StringMap& field_expr_map = StringMap())
            : sort_(sort)
            , ku_str_(ku_str)
            , param_types_(param_types)
            , field_expr_map_(field_expr_map) {}

        bool operator<(const QueryFamily& other) const {
            if (sort_ != other.sort_)
                return sort_ < other.sort_;
            if (ku_str_.size() != other.ku_str_.size())
                return ku_str_.size() < other.ku_str_.size();
            int comp = ku_str_.compare(other.ku_str_);
            if (comp)
                return comp < 0;
            if (param_types_.size() != other.param_types_.size())
                return param_types_.size() < other.param_types_.size();
            for (size_t i = 0; i < param_types_.size(); ++i)
                if (param_types_[i] != other.param_types_[i])
                    return param_types_[i] < other.param_types_[i];
            if (field_expr_map_.size() != other.field_expr_map_.size())
                return field_expr_map_.size() < other.field_expr_map_.size();
            return field_expr_map_ < other.field_expr_map_;
        }

        bool operator==(const QueryFamily& other) const {
            return (sort_ == other.sort_ &&
                    ku_str_ == other.ku_str_ &&
                    param_types_ == other.param_types_ &&
                    field_expr_map_ == other.field_expr_map_);
        }

        Sort GetSort() const {
            return sort_;
        }

        const string& GetKuStr() const {
            return ku_str_;
        }

        const Types& GetParamTypes() const {
            return param_types_;
        }

        const StringMap& GetFieldExprMap() const {
            return field_expr_map_;
        }
        
    private:
        Sort sort_;
        string ku_str_;
        Types param_types_;
        StringMap field_expr_map_;
    };
}

////////////////////////////////////////////////////////////////////////////////
// SpecifierFamily
////////////////////////////////////////////////////////////////////////////////
    
namespace
{
    /// The family of specifiers which could be performed through one
    /// prepared statement
    class SpecifierFamily : private
    boost::less_than_comparable<
        SpecifierFamily,
        boost::equality_comparable<SpecifierFamily> > {
    public:
        enum Sort {
            QUERY,
            WHERE,
            BY,
            ONLY
        };

        SpecifierFamily(Sort sort, const string& ku_str, const Types& param_types)
            : sort_(sort), ku_str_(ku_str), param_types_(param_types) {}

        bool operator<(const SpecifierFamily& other) const {
            if (sort_ != other.sort_)
                return sort_ < other.sort_;
            if (ku_str_.size() != other.ku_str_.size())
                return ku_str_.size() < other.ku_str_.size();
            int comp = ku_str_.compare(other.ku_str_);
            if (comp)
                return comp < 0;
            if (param_types_.size() != other.param_types_.size())
                return param_types_.size() < other.param_types_.size();
            return param_types_ < other.param_types_;
        }

        bool operator==(const SpecifierFamily& other) const {
            return (sort_ == other.sort_ &&
                    ku_str_ == other.ku_str_ &&
                    param_types_ == other.param_types_);
        }

        Sort GetSort() const {
            return sort_;
        }

        const string& GetKuStr() const {
            return ku_str_;
        }

        const Types& GetParamTypes() const {
            return param_types_;
        }

    private:
        Sort sort_;
        string ku_str_;
        Types param_types_;
    };

    
    typedef vector<SpecifierFamily> SpecifierFamilies;
    

    class SpecifierFamilyGetter : public static_visitor<SpecifierFamily> {
    public:
        SpecifierFamily operator()(const WhereSpecifier& where_spec) const {
            return SpecifierFamily(SpecifierFamily::WHERE,
                                   where_spec.expr_str,
                                   GetValuesTypes(where_spec.params));
        }
        
        SpecifierFamily operator()(const BySpecifier& by_spec) const {
            return SpecifierFamily(SpecifierFamily::BY,
                                   by_spec.expr_str,
                                   GetValuesTypes(by_spec.params));
        }

        SpecifierFamily operator()(const OnlySpecifier& only_spec) const {
            ostringstream oss;
            OmitInvoker print_sep((SepPrinter(oss)));
            BOOST_FOREACH(const string& field_name, only_spec.field_names) {
                print_sep();
                oss << field_name;
            }
            return SpecifierFamily(SpecifierFamily::ONLY,
                                   oss.str(),
                                   Types());
        }
    };

    
    SpecifierFamilies GetSpecifierFamilies(const Specifiers& specifiers)
    {
        SpecifierFamilies result;
        result.reserve(specifiers.size());
        BOOST_FOREACH(const Specifier& specifier, specifiers)
            result.push_back(apply_visitor(SpecifierFamilyGetter(), specifier));
        return result;
    }


    SpecifierFamilies
    GetSpecifierFamilies(const WhereSpecifiers& where_specifiers)
    {
        SpecifierFamilies result;
        result.reserve(where_specifiers.size());
        BOOST_FOREACH(const WhereSpecifier& where_specifier, where_specifiers)
            result.push_back(SpecifierFamilyGetter()(where_specifier));
        return result;
    }
}

////////////////////////////////////////////////////////////////////////////////
// GetSpecifierValues
////////////////////////////////////////////////////////////////////////////////

namespace
{
    class SpecifierValuesGetter : public static_visitor<const Values*> {
    public:
        const Values* operator()(const WhereSpecifier& where_spec) const {
            return &where_spec.params;
        }

        const Values* operator()(const BySpecifier& by_spec) const {
            return &by_spec.params;
        }

        const Values* operator()(const OnlySpecifier& /*only_spec*/) const {
            return 0;
        }
    };


    const Values& GetSpecifierValues(const Specifier& specifier)
    {
        static const Values empty_values;
        const Values* values_ptr = apply_visitor(SpecifierValuesGetter(),
                                                 specifier);
        return values_ptr ? *values_ptr : empty_values;
    }

    
    const Values& GetSpecifierValues(const WhereSpecifier& where_specifier)
    {
        return where_specifier.params;
    }
}

////////////////////////////////////////////////////////////////////////////////
// ItemCollector
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Collects items from query and specifiers for subsequent translation
    class ItemCollector : public static_visitor<void>
                        , private noncopyable {
    public:
        ItemCollector(Quoter* quoter_ptr, size_t param_shift)
            : quoter_ptr_(quoter_ptr)
            , param_shift_(param_shift) {}
        
        void operator()(const WhereSpecifier& where_specifier) {
            where_items_.push_back(
                TranslateItem(where_specifier.expr_str,
                              GetValuesTypes(where_specifier.params),
                              param_shift_,
                              GetParamStrings(where_specifier.params)));
            param_shift_ += where_specifier.params.size();
        }

        void operator()(const BySpecifier& by_specifier) {
            by_items_.push_back(
                TranslateItem(by_specifier.expr_str,
                              GetValuesTypes(by_specifier.params),
                              param_shift_,
                              GetParamStrings(by_specifier.params)));
            param_shift_ += by_specifier.params.size();
        }

        void operator()(const OnlySpecifier& only_specifier) {
            only_fields_ptr_.reset(new StringSet(only_specifier.field_names));
        }

        const TranslateItems GetWhereItems() const {
            return where_items_;
        }
        
        const TranslateItems GetByItems() const {
            return by_items_;
        }

        const StringSet* GetOnlyFieldsPtr() const {
            return only_fields_ptr_.get();
        }
        
    private:
        auto_ptr<Quoter> quoter_ptr_;
        size_t param_shift_;
        TranslateItems where_items_;
        TranslateItems by_items_;
        auto_ptr<StringSet> only_fields_ptr_;

        Strings GetParamStrings(const Values& params) const {
            return quoter_ptr_.get() ? (*quoter_ptr_)(params) : Strings();
        }
    };
}

////////////////////////////////////////////////////////////////////////////////
// QueryMap
////////////////////////////////////////////////////////////////////////////////

namespace
{
    /// Class for mapping a tuple (QueryFamily, SpecifierFamilies) to
    /// values of any type
    template <typename T>
    class QueryMap {
    public:
        QueryMap();
        
        T* GetItemPtr(const QueryFamily& qf,
                      const SpecifierFamilies& sfs);
        
        T& SetItem(const QueryFamily& qf,
                   const SpecifierFamilies& sfs,
                   const T& t,
                   bool overwrite);

        size_t GetSize() const;
        void Clear();

    private:
        struct Entry {
            typedef map<SpecifierFamily, Entry> SFMap;

            SFMap sf_map;
            Wrapper<T> t_wr;
        };
            
        typedef map<QueryFamily, Entry> QFMap;

        QFMap qf_map_;
        size_t size_;
    };
}


template <typename T>
QueryMap<T>::QueryMap()
    : size_(0)
{
}


template <typename T>
T* QueryMap<T>::GetItemPtr(const QueryFamily& qf,
                           const SpecifierFamilies& sfs)
{
    typename QFMap::iterator itr = qf_map_.find(qf);
    if (itr == qf_map_.end())
        return 0;
    Entry* entry_ptr = &itr->second;
    BOOST_FOREACH(const SpecifierFamily& sf, sfs) {
        typename Entry::SFMap::iterator itr = entry_ptr->sf_map.find(sf);
        if (itr == entry_ptr->sf_map.end())
            return 0;
        entry_ptr = &itr->second;
    }
    return entry_ptr->t_wr.GetPtr();
}


template <typename T>
T& QueryMap<T>::SetItem(const QueryFamily& qf,
                        const SpecifierFamilies& sfs,
                        const T& t,
                        bool overwrite)
{
    typename QFMap::iterator qf_map_itr = qf_map_.find(qf);
    SpecifierFamilies::const_iterator sfs_itr = sfs.begin();
    Entry* entry_ptr;
    if (qf_map_itr == qf_map_.end()) {
        entry_ptr =
            &qf_map_.insert(
                typename QFMap::value_type(qf, Entry())).first->second;
    } else {
        entry_ptr = &qf_map_itr->second;
        for (; sfs_itr != sfs.end(); ++sfs_itr) {
            typename Entry::SFMap::iterator
                sf_map_itr = entry_ptr->sf_map.find(*sfs_itr);
            if (sf_map_itr == entry_ptr->sf_map.end())
                break;
            entry_ptr = &sf_map_itr->second;
        }
    }
    for (; sfs_itr != sfs.end(); ++sfs_itr) {
        entry_ptr =
            &entry_ptr->sf_map.insert(
                typename Entry::SFMap::value_type(*sfs_itr,
                                                  Entry())).first->second;
    }
    if (!entry_ptr->t_wr.GetPtr())
        ++size_;
    if (!entry_ptr->t_wr.GetPtr() || overwrite)
        entry_ptr->t_wr = t;
    return entry_ptr->t_wr.Get();
}


template <typename T>
size_t QueryMap<T>::GetSize() const
{
    return size_;
}


template <typename T>
void QueryMap<T>::Clear()
{
    qf_map_.clear();
    size_ = 0;
}

////////////////////////////////////////////////////////////////////////////////
// Querist::Impl declaration
////////////////////////////////////////////////////////////////////////////////

/// Querist implementation
class Querist::Impl : private noncopyable {
public:
    Impl(const DBViewer& db_viewer, pqxx::connection& conn);
    ~Impl();

    QueryResult Query(pqxx::transaction_base& work,
                      const string& query_str,
                      const Values& params,
                      const Specifiers& specifiers);

    void Update(pqxx::transaction_base& work,
                const string& rel_name,
                const StringMap& field_expr_map,
                const Values& params,
                const WhereSpecifiers& where_specifiers);
                    
    void Delete(pqxx::transaction_base& work,
                const string& rel_name,
                const WhereSpecifiers& where_specifiers);

    void ClearCache();

    string TranslateExpr(const string& ku_expr_str,
                         const string& rel_name,
                         const Header& rel_header) const;
    
private:
    template <typename SpecsT>
    class SqlFunctor;
    
    struct Prepared {
        size_t number;
        Header header;

        Prepared(const size_t& number, const Header& header)
            : number(number), header(header) {}
    };

    Translator translator_;
    pqxx::connection& conn_;
    QueryMap<Prepared> prepared_map_;
    QueryMap<size_t> use_count_map_;
    size_t prepared_count_;

    void ClearUseCountMap();
    void ClearPreparedMap();    
};

////////////////////////////////////////////////////////////////////////////////
// Querist::Impl::SqlFunctor
////////////////////////////////////////////////////////////////////////////////

/// SQL query perform functor
template <typename SpecsT>
class Querist::Impl::SqlFunctor {
public:
    SqlFunctor(Querist::Impl& querist_impl,
               const QueryFamily& query_family_,
               const Values& params,
               const SpecsT& specs);

    QueryResult operator()(pqxx::transaction_base& work) const;
private:
    typedef typename SpecsT::value_type SpecT;

    Querist::Impl& querist_impl_;
    const QueryFamily& query_family_;
    const Values& params_;
    const SpecsT& specs_;
    SpecifierFamilies specifier_families_;

    Translation Translate(bool raw) const;
    const Prepared& Prepare(const Translation& translation) const;
    TranslateItem GetQueryItem(bool raw) const;

    QueryResult ExecPrepared(pqxx::transaction_base& work,
                             const Prepared& prepared) const;
    
    QueryResult ExecRaw(pqxx::transaction_base& work,
                        const Translation& translation) const;
};


template <typename SpecsT>
Querist::Impl::SqlFunctor<SpecsT>::SqlFunctor(Querist::Impl& querist_impl,
                                              const QueryFamily& query_family,
                                              const Values& params,
                                              const SpecsT& specs)
    : querist_impl_(querist_impl)
    , query_family_(query_family)
    , params_(params)
    , specs_(specs)
    , specifier_families_(GetSpecifierFamilies(specs))
{
}


template <typename SpecsT>
QueryResult
Querist::Impl::SqlFunctor<SpecsT>::operator()(pqxx::transaction_base& work) const
{
    if (querist_impl_.use_count_map_.GetSize() > USE_COUNT_MAP_CLEAR_SIZE)
        querist_impl_.ClearUseCountMap();
    const Prepared* prepared_ptr =
        querist_impl_.prepared_map_.GetItemPtr(query_family_,
                                               specifier_families_);
    if (prepared_ptr)
        return ExecPrepared(work, *prepared_ptr);
    size_t& use_count(querist_impl_.use_count_map_.SetItem(query_family_,
                                                           specifier_families_,
                                                           0,
                                                           false));
    if (++use_count > PREPARATION_USE_COUNT) {
        Translation translation(Translate(false));
        const Prepared& prepared(Prepare(translation));
        return ExecPrepared(work, prepared);
    }
    Translation translation(Translate(true));
    return ExecRaw(work, translation);
}


namespace ku
{
    template <>
    Translation
    Querist::Impl::SqlFunctor<Specifiers>::Translate(bool raw) const
    {
        KU_ASSERT(query_family_.GetSort() == QueryFamily::QUERY);
        ItemCollector item_collector(raw ? new Quoter(querist_impl_.conn_) : 0,
                                     params_.size());
        BOOST_FOREACH(const Specifier& specifier, specs_)
            apply_visitor(item_collector, specifier);
        return querist_impl_.translator_.TranslateQuery(
            GetQueryItem(raw),
            item_collector.GetWhereItems(),
            item_collector.GetByItems(),
            item_collector.GetOnlyFieldsPtr());
    }

    
    template <>
    Translation
    Querist::Impl::SqlFunctor<WhereSpecifiers>::Translate(bool raw) const
    {
        ItemCollector item_collector(raw ? new Quoter(querist_impl_.conn_) : 0,
                                     params_.size());
        BOOST_FOREACH(const WhereSpecifier& where_specifier, specs_)
            item_collector(where_specifier);
        if (query_family_.GetSort() == QueryFamily::DELETE) {
            string sql_str(querist_impl_.translator_.TranslateDelete(
                               query_family_.GetKuStr(),
                               item_collector.GetWhereItems()));
            return Translation(sql_str, Header());
        }
        KU_ASSERT(query_family_.GetSort() == QueryFamily::UPDATE);
        string sql_str(querist_impl_.translator_.TranslateUpdate(
                           GetQueryItem(raw),
                           query_family_.GetFieldExprMap(),
                           item_collector.GetWhereItems()));
        return Translation(sql_str, Header());
    }
}


template <typename SpecsT>
TranslateItem Querist::Impl::SqlFunctor<SpecsT>::GetQueryItem(bool raw) const
{
    Strings param_strings(raw
                          ? Quoter(querist_impl_.conn_)(params_)
                          : Strings());        
    return TranslateItem(query_family_.GetKuStr(),
                         query_family_.GetParamTypes(),
                         0,
                         param_strings);
}


template <typename SpecsT>
const Querist::Impl::Prepared&
Querist::Impl::SqlFunctor<SpecsT>::Prepare(const Translation& translation) const
{
    if (querist_impl_.prepared_map_.GetSize() > PREPARED_MAP_CLEAR_SIZE)
        querist_impl_.ClearPreparedMap();
    Prepared prepared(querist_impl_.prepared_count_++, translation.header);
    pqxx::prepare::declaration
        declaration(querist_impl_.conn_.prepare(
                        lexical_cast<string>(prepared.number),
                        translation.sql_str));
    BOOST_FOREACH(const Type& type, query_family_.GetParamTypes())
        declaration(type.GetPgStr(), pqxx::prepare::treat_direct);
    BOOST_FOREACH(const SpecifierFamily& specifier_family,
                  specifier_families_) {
        BOOST_FOREACH(const Type& type, specifier_family.GetParamTypes())
            declaration(type.GetPgStr(), pqxx::prepare::treat_direct);
    }
    return querist_impl_.prepared_map_.SetItem(query_family_,
                                               specifier_families_,
                                               prepared,
                                               true);
}


template <typename SpecsT>
QueryResult
Querist::Impl::SqlFunctor<SpecsT>::ExecPrepared(pqxx::transaction_base& work,
                                                const Prepared& prepared) const
{
    pqxx::prepare::invocation
        invocation(work.prepared(lexical_cast<string>(prepared.number)));
    BOOST_FOREACH(const Value& value, params_)
        invocation(value.GetPgLiter().str);
    BOOST_FOREACH(const SpecT& spec, specs_) {
        const Values& values(GetSpecifierValues(spec));
        BOOST_FOREACH(const Value& value, values)
            invocation(value.GetPgLiter().str);
    }
    pqxx::result pqxx_result(invocation.exec());
    return QueryResult(new QueryResult::Data(prepared.header, pqxx_result));
}


template <typename SpecsT>
QueryResult
Querist::Impl::SqlFunctor<SpecsT>::ExecRaw(pqxx::transaction_base& work,
                                           const Translation& translation) const
{
    pqxx::result pqxx_result(work.exec(translation.sql_str));
    return QueryResult(new QueryResult::Data(translation.header, pqxx_result));
}

////////////////////////////////////////////////////////////////////////////////
// Querist::Impl definitions
////////////////////////////////////////////////////////////////////////////////

Querist::Impl::Impl(const DBViewer& db_viewer, pqxx::connection& conn)
    : translator_(db_viewer)
    , conn_(conn)
    , prepared_count_(0)
{
}


Querist::Impl::~Impl()
{
    ClearCache();
}


QueryResult Querist::Impl::Query(pqxx::transaction_base& work,
                                 const string& query_str,
                                 const Values& params,
                                 const Specifiers& specifiers)
{
    QueryFamily query_family(QueryFamily::QUERY,
                             query_str,
                             GetValuesTypes(params));
    SqlFunctor<Specifiers>
        sql_functor(*this, query_family, params, specifiers);
    return sql_functor(work);
}


void Querist::Impl::Update(pqxx::transaction_base& work,
                           const string& rel_name,
                           const StringMap& field_expr_map,
                           const Values& params,
                           const WhereSpecifiers& where_specifiers)
{
    QueryFamily query_family(QueryFamily::UPDATE,
                             rel_name,
                             GetValuesTypes(params),
                             field_expr_map);
    SqlFunctor<WhereSpecifiers>
        sql_functor(*this, query_family, params, where_specifiers);
    sql_functor(work);
}


void Querist::Impl::Delete(pqxx::transaction_base& work,
                           const string& rel_name,
                           const WhereSpecifiers& where_specifiers)
{
    QueryFamily query_family(QueryFamily::DELETE, rel_name);
    SqlFunctor<WhereSpecifiers>
        sql_functor(*this, query_family, Values(), where_specifiers);
    sql_functor(work);
}

void Querist::Impl::ClearCache()
{
    ClearUseCountMap();
    ClearPreparedMap();
}


string Querist::Impl::TranslateExpr(const string& ku_expr_str,
                                    const string& rel_name,
                                    const Header& rel_header) const
{
    return translator_.TranslateExpr(ku_expr_str, rel_name, rel_header);
}


void Querist::Impl::ClearUseCountMap()
{
    use_count_map_.Clear();
}


void Querist::Impl::ClearPreparedMap()
{
    prepared_map_.Clear();
    for (size_t i = 0; i < prepared_count_; ++i)
        conn_.unprepare(lexical_cast<string>(i));
    prepared_count_ = 0;
}

////////////////////////////////////////////////////////////////////////////////
// Querist definitions
////////////////////////////////////////////////////////////////////////////////

Querist::Querist(const DBViewer& db_viewer, pqxx::connection& conn)
    : pimpl_(new Impl(db_viewer, conn))
{
}


Querist::~Querist()
{
}


QueryResult Querist::Query(pqxx::transaction_base& work,
                           const string& query_str,
                           const Values& params,
                           const Specifiers& specifiers)
{
    return pimpl_->Query(work, query_str, params, specifiers);
}


void Querist::Update(pqxx::transaction_base& work,
                     const string& rel_name,
                     const StringMap& field_expr_map,
                     const Values& params,
                     const WhereSpecifiers& where_specifiers)
{
    return pimpl_->Update(work, rel_name, field_expr_map,
                          params, where_specifiers);
}


void Querist::Delete(pqxx::transaction_base& work,
                     const string& rel_name,
                     const WhereSpecifiers& where_specifiers)
{
    return pimpl_->Delete(work, rel_name, where_specifiers);
}


void Querist::ClearCache()
{
    pimpl_->ClearCache();
}


string Querist::TranslateExpr(const string& ku_expr_str,
                              const string& rel_name,
                              const Header& rel_header) const
{
    return pimpl_->TranslateExpr(ku_expr_str, rel_name, rel_header);
}
