
// (c) 2008 by Anton Korenyushkin

/// \file querist.cc
/// Database query impl


#include "querist.h"
#include "translator.h"
#include "utils.h"

#include <boost/lexical_cast.hpp>
#include <boost/utility.hpp>


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
    const size_t PREPARATION_USE_COUNT = 5;
#ifdef TEST
    const size_t PREPARED_MAP_CLEAR_SIZE = 10;
    const size_t USE_COUNT_MAP_CLEAR_SIZE = 20;
#else
    const size_t PREPARED_MAP_CLEAR_SIZE = 500;
    const size_t USE_COUNT_MAP_CLEAR_SIZE = 1000;
#endif

    const size_t AVERAGING_TUPLE_COUNT = 10;
}

////////////////////////////////////////////////////////////////////////////////
// GetTupleValues definition
////////////////////////////////////////////////////////////////////////////////

Values ku::GetTupleValues(const pqxx::result::tuple& tuple,
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
        else if (type == Type::BOOLEAN)
            result.push_back(Value(type, field.as<bool>()));
        else
            result.push_back(Value(type, field.as<string>()));
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////
// QueryResult::Impl
////////////////////////////////////////////////////////////////////////////////

class QueryResult::Impl : private noncopyable {
public:
    Impl(const Header& header, const pqxx::result& pqxx_result);
    const Header& GetHeader() const;
    const pqxx::result& GetPqxxResult() const;
    size_t GetMemoryUsage() const;
    
private:
    Header header_;
    pqxx::result pqxx_result_;
    size_t memory_usage_;
};


QueryResult::Impl::Impl(const Header& header, const pqxx::result& pqxx_result)
    : header_(header), pqxx_result_(pqxx_result)
{
    // NB may be memory usage should not be calculated excactly due to
    // speed considerations
    memory_usage_ = 0;
    BOOST_FOREACH(const pqxx::result::tuple& tuple, pqxx_result_) {
        KU_ASSERT((tuple.size() == header_.size()) ||
                  (tuple.size() == 1 && header_.size() == 0)); // zero-column
        BOOST_FOREACH(const pqxx::result::field& field, tuple)
            memory_usage_ += field.size();
    }
}


const Header& QueryResult::Impl::GetHeader() const
{
    return header_;
}


const pqxx::result& QueryResult::Impl::GetPqxxResult() const
{
    return pqxx_result_;
}


size_t QueryResult::Impl::GetMemoryUsage() const
{
    return memory_usage_;
}

////////////////////////////////////////////////////////////////////////////////
// QueryResult definitions
////////////////////////////////////////////////////////////////////////////////

QueryResult::QueryResult(const Impl* impl_ptr)
    : impl_ptr_(impl_ptr)
{
}


QueryResult::~QueryResult()
{
}


size_t QueryResult::GetSize() const
{
    if (impl_ptr_->GetHeader().empty())
        return impl_ptr_->GetPqxxResult().empty() ? 0 : 1;
    return impl_ptr_->GetPqxxResult().size();
}


auto_ptr<Values> QueryResult::GetValuesPtr(size_t idx) const
{
    if (idx >= GetSize())
        return auto_ptr<Values>();
    if (impl_ptr_->GetHeader().empty()) {
        KU_ASSERT(GetSize() == 1);
        return auto_ptr<Values>(new Values());
    }
    return auto_ptr<Values>(
        new Values(GetTupleValues(impl_ptr_->GetPqxxResult()[idx],
                                  GetHeader())));
}


const Header& QueryResult::GetHeader() const
{
    return impl_ptr_->GetHeader();
}


size_t QueryResult::GetMemoryUsage() const
{
    return impl_ptr_->GetMemoryUsage();
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
            COUNT,
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
            WHERE,
            BY,
            ONLY,
            WINDOW
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
    

    class SpecifierFamilyCollector : public static_visitor<void>
                                   , private noncopyable {
    public:
        SpecifierFamilyCollector(QueryFamily::Sort query_sort)
            : query_sort_(query_sort) {}
        
        void operator()(const WhereSpecifier& where_spec) {
            result_.push_back(
                SpecifierFamily(SpecifierFamily::WHERE,
                                where_spec.expr_str,
                                GetValuesTypes(where_spec.params)));
        }
        
        void operator()(const BySpecifier& by_spec) {
            if (query_sort_ != QueryFamily::COUNT)
                result_.push_back(
                    SpecifierFamily(SpecifierFamily::BY,
                                    by_spec.expr_str,
                                    GetValuesTypes(by_spec.params)));
        }

        void operator()(const OnlySpecifier& only_spec) {
            if (query_sort_ == QueryFamily::COUNT)
                return;
            ostringstream oss;
            OmitInvoker print_sep((SepPrinter(oss)));
            BOOST_FOREACH(const string& field_name, only_spec.field_names) {
                print_sep();
                oss << field_name;
            }
            result_.push_back(
                SpecifierFamily(SpecifierFamily::ONLY, oss.str(), Types()));
        }
        
        void operator()(const WindowSpecifier& /*win_spec*/) {
            result_.push_back(
                SpecifierFamily(SpecifierFamily::WINDOW, "", Types()));
        }

        const SpecifierFamilies& GetResult() const {
            return result_;
        }

    private:
        QueryFamily::Sort query_sort_;
        SpecifierFamilies result_;
    };

    
    SpecifierFamilies GetSpecifierFamilies(QueryFamily::Sort query_sort,
                                           const Specifiers& specifiers)
    {
        SpecifierFamilyCollector collector(query_sort);
        BOOST_FOREACH(const Specifier& specifier, specifiers)
            apply_visitor(collector, specifier);
        return collector.GetResult();
    }


    SpecifierFamilies
    GetSpecifierFamilies(QueryFamily::Sort query_sort,
                         const WhereSpecifiers& where_specifiers)
    {
        SpecifierFamilyCollector collector(query_sort);
        BOOST_FOREACH(const WhereSpecifier& where_specifier, where_specifiers)
            collector(where_specifier);
        return collector.GetResult();
    }
}

////////////////////////////////////////////////////////////////////////////////
// ItemCollector
////////////////////////////////////////////////////////////////////////////////

namespace
{
    void AdjustOffsetAndLimit(unsigned long& offset,
                              unsigned long& limit,
                              unsigned long sub_offset,
                              unsigned long sub_limit)
    {
        if (limit <= sub_offset) {
            limit = 0;
        } else {
            offset += sub_offset;
            limit = min(sub_limit, limit - sub_offset);
        }
    }

    
    /// Collects items from query and specifiers for subsequent translation
    class ItemCollector : public static_visitor<void>
                        , private noncopyable {
    public:
        ItemCollector(QueryFamily::Sort query_sort, const Quoter& quoter)
            : query_sort_(query_sort)
            , quoter_ptr_(new Quoter(quoter))
            , param_shift_(RAW_SHIFT) {}
        
        ItemCollector(QueryFamily::Sort query_sort, size_t param_shift)
            : query_sort_(query_sort)
            , param_shift_(param_shift) {}
            
        void operator()(const WhereSpecifier& where_spec) {
            where_items_.push_back(
                MakeTranslateItem(where_spec.expr_str, where_spec.params));
        }

        void operator()(const BySpecifier& by_spec) {
            if (query_sort_ != QueryFamily::COUNT)
                by_items_.push_back(
                    MakeTranslateItem(by_spec.expr_str, by_spec.params));
        }
        
        void operator()(const OnlySpecifier& only_spec) {
            if (query_sort_ != QueryFamily::COUNT)
                only_fields_ptr_.reset(new StringSet(only_spec.field_names));
        }

        void operator()(const WindowSpecifier& window_spec) {
            if (window_ptr_.get()) {
                if (param_shift_ == RAW_SHIFT)
                    AdjustOffsetAndLimit(window_ptr_->offset,
                                         window_ptr_->limit,
                                         window_spec.offset,
                                         window_spec.limit);
            } else {
                if (param_shift_ == RAW_SHIFT) {
                    window_ptr_.reset(
                        new Window(window_spec.offset, window_spec.limit));
                } else {
                    window_ptr_.reset(new Window(param_shift_));
                    param_shift_ += 2;
                }
            }
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

        const Window* GetWindowPtr() const {
            return window_ptr_.get();
        }
        
    private:
        QueryFamily::Sort query_sort_;
        auto_ptr<Quoter> quoter_ptr_;
        size_t param_shift_;
        TranslateItems where_items_;
        TranslateItems by_items_;
        auto_ptr<StringSet> only_fields_ptr_;
        auto_ptr<Window> window_ptr_;

        TranslateItem MakeTranslateItem(const string& ku_str,
                                        const Values& params) {
            Types types(GetValuesTypes(params));
            if (param_shift_ == RAW_SHIFT)
                return TranslateItem(ku_str, types, (*quoter_ptr_)(params));
            TranslateItem result(ku_str, types, param_shift_);
            param_shift_ += params.size();
            return result;
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

    unsigned long Count(pqxx::transaction_base& work,
                        const string& query_str,
                        const Values& params,
                        const Specifiers& specifiers);
    
    unsigned long Update(pqxx::transaction_base& work,
                         const string& rel_name,
                         const StringMap& field_expr_map,
                         const Values& params,
                         const WhereSpecifiers& where_specifiers);
                    
    unsigned long Delete(pqxx::transaction_base& work,
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

    auto_ptr<QueryResult::Impl> operator()(pqxx::transaction_base& work) const;
    
private:
    typedef typename SpecsT::value_type SpecT;

    Querist::Impl& querist_impl_;
    const QueryFamily& query_family_;
    const Values& params_;
    const SpecsT& specs_;
    SpecifierFamilies specifier_families_;

    auto_ptr<ItemCollector> MakeItemCollector(bool raw) const;
    TranslateItem GetQueryItem(bool raw) const;
    Translation Translate(bool raw) const;
    const Prepared& Prepare(const Translation& translation) const;

    void TransmitSpecifiersValues(pqxx::prepare::invocation& invocation) const;
    auto_ptr<QueryResult::Impl> ExecPrepared(pqxx::transaction_base& work,
                                             const Prepared& prepared) const;
    
    auto_ptr<QueryResult::Impl> ExecRaw(pqxx::transaction_base& work,
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
    , specifier_families_(GetSpecifierFamilies(query_family.GetSort(), specs))
{
}


template <typename SpecsT>
auto_ptr<QueryResult::Impl>
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


template <typename SpecsT>
auto_ptr<ItemCollector>
Querist::Impl::SqlFunctor<SpecsT>::MakeItemCollector(bool raw) const
{
    QueryFamily::Sort sort(query_family_.GetSort());
    return auto_ptr<ItemCollector>(
        raw
        ? new ItemCollector(sort, Quoter(querist_impl_.conn_))
        : new ItemCollector(sort, params_.size()));
}


template <typename SpecsT>
TranslateItem Querist::Impl::SqlFunctor<SpecsT>::GetQueryItem(bool raw) const
{
    string ku_str(query_family_.GetKuStr());
    Types types(query_family_.GetParamTypes());
    return (raw
            ? TranslateItem(ku_str, types, Quoter(querist_impl_.conn_)(params_))
            : TranslateItem(ku_str, types, 0));
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
    bool window_visited = false;
    BOOST_FOREACH(const SpecifierFamily& specifier_family,
                  specifier_families_) {
        if (specifier_family.GetSort() == SpecifierFamily::WINDOW) {
            if (!window_visited) {
                declaration("int8", pqxx::prepare::treat_direct);
                declaration("int8", pqxx::prepare::treat_direct);
                window_visited = true;
            }
        } else {
            BOOST_FOREACH(const Type& type, specifier_family.GetParamTypes())
                declaration(type.GetPgStr(), pqxx::prepare::treat_direct);
        }
    }
    return querist_impl_.prepared_map_.SetItem(query_family_,
                                               specifier_families_,
                                               prepared,
                                               true);
}


namespace
{
    void TransmitValues(pqxx::prepare::invocation& invocation,
                        const Values& values)
    {
        BOOST_FOREACH(const Value& value, values)
            invocation(value.GetPgLiter().str);
    }
}


namespace ku
{
    template <>
    Translation
    Querist::Impl::SqlFunctor<Specifiers>::Translate(bool raw) const
    {
        QueryFamily::Sort sort = query_family_.GetSort();
        auto_ptr<ItemCollector> item_collector_ptr(MakeItemCollector(raw));
        BOOST_FOREACH(const Specifier& specifier, specs_)
            apply_visitor(*item_collector_ptr, specifier);
        if (sort == QueryFamily::QUERY) {
            return querist_impl_.translator_.TranslateQuery(
                GetQueryItem(raw),
                item_collector_ptr->GetWhereItems(),
                item_collector_ptr->GetByItems(),
                item_collector_ptr->GetOnlyFieldsPtr(),
                item_collector_ptr->GetWindowPtr());
        } else {
            KU_ASSERT(sort == QueryFamily::COUNT);
            return Translation(
                querist_impl_.translator_.TranslateCount(
                    GetQueryItem(raw),
                    item_collector_ptr->GetWhereItems(),
                    item_collector_ptr->GetWindowPtr()),
                Header());
        }
    }

    
    template <>
    Translation
    Querist::Impl::SqlFunctor<WhereSpecifiers>::Translate(bool raw) const
    {
        QueryFamily::Sort sort = query_family_.GetSort();
        auto_ptr<ItemCollector> item_collector_ptr(MakeItemCollector(raw));
        BOOST_FOREACH(const WhereSpecifier& where_specifier, specs_)
            (*item_collector_ptr)(where_specifier);
        string sql_str;
        if (sort == QueryFamily::DELETE) {
            sql_str = querist_impl_.translator_.TranslateDelete(
                query_family_.GetKuStr(),
                item_collector_ptr->GetWhereItems());
        } else {
            KU_ASSERT(sort == QueryFamily::UPDATE);
            sql_str = querist_impl_.translator_.TranslateUpdate(
                GetQueryItem(raw),
                query_family_.GetFieldExprMap(),
                item_collector_ptr->GetWhereItems());
        }
        return Translation(sql_str, Header());
    }
    
    
    template <>
    void Querist::Impl::SqlFunctor<Specifiers>::TransmitSpecifiersValues(
        pqxx::prepare::invocation& invocation) const
    {
        bool window_visited = false;
        for (size_t i = 0; i < specs_.size(); ++i) {
            const Specifier& spec(specs_[i]);
            const WindowSpecifier* window_spec_ptr;
            if (!window_visited &&
                (window_spec_ptr = boost::get<WindowSpecifier>(&spec))) {
                unsigned long offset = window_spec_ptr->offset;
                unsigned long limit = window_spec_ptr->limit;
                for (size_t j = i + 1; j < specs_.size(); ++j) {
                    if (const WindowSpecifier* another_window_spec_ptr =
                        boost::get<WindowSpecifier>(&specs_[j]))
                        AdjustOffsetAndLimit(offset,
                                             limit,
                                             another_window_spec_ptr->offset,
                                             another_window_spec_ptr->limit);
                }
                invocation(lexical_cast<string>(limit));
                invocation(lexical_cast<string>(offset));
                window_visited = true;
            } else if (const WhereSpecifier* where_spec_ptr =
                       boost::get<WhereSpecifier>(&spec)) {
                TransmitValues(invocation, where_spec_ptr->params);
            } else if (query_family_.GetSort() != QueryFamily::COUNT) {
                if (const BySpecifier* by_spec_ptr =
                    boost::get<BySpecifier>(&spec))
                    TransmitValues(invocation, by_spec_ptr->params);
            }
        }
    }
    
    
    template <>
    void Querist::Impl::SqlFunctor<WhereSpecifiers>::TransmitSpecifiersValues(
        pqxx::prepare::invocation& invocation) const
    {
        BOOST_FOREACH(const WhereSpecifier& where_spec, specs_)
            TransmitValues(invocation, where_spec.params);
    }
}


template <typename SpecsT>
auto_ptr<QueryResult::Impl>
Querist::Impl::SqlFunctor<SpecsT>::ExecPrepared(pqxx::transaction_base& work,
                                                const Prepared& prepared) const
{
    pqxx::prepare::invocation
        invocation(work.prepared(lexical_cast<string>(prepared.number)));
    BOOST_FOREACH(const Value& value, params_)
        invocation(value.GetPgLiter().str);
    TransmitSpecifiersValues(invocation);
    pqxx::result pqxx_result(invocation.exec());
    return auto_ptr<QueryResult::Impl>(new QueryResult::Impl(prepared.header,
                                                             pqxx_result));
}


template <typename SpecsT>
auto_ptr<QueryResult::Impl>
Querist::Impl::SqlFunctor<SpecsT>::ExecRaw(pqxx::transaction_base& work,
                                           const Translation& translation) const
{
    pqxx::result pqxx_result(work.exec(translation.sql_str));
    return auto_ptr<QueryResult::Impl>(new QueryResult::Impl(translation.header,
                                                             pqxx_result));
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
    SqlFunctor<Specifiers> sql_functor(*this, query_family, params, specifiers);
    return QueryResult(sql_functor(work).release());
}


unsigned long Querist::Impl::Count(pqxx::transaction_base& work,
                                   const string& query_str,
                                   const Values& params,
                                   const Specifiers& specifiers)
{
    QueryFamily query_family(QueryFamily::COUNT,
                             query_str,
                             GetValuesTypes(params));
    SqlFunctor<Specifiers> sql_functor(*this, query_family, params, specifiers);
    auto_ptr<QueryResult::Impl> query_result_impl_ptr(sql_functor(work));
    const pqxx::result& pqxx_result(query_result_impl_ptr->GetPqxxResult());
    KU_ASSERT(pqxx_result.size() == 1 && pqxx_result[0].size() == 1);
    return pqxx_result[0][0].as<unsigned long>();
}


unsigned long Querist::Impl::Update(pqxx::transaction_base& work,
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
    return sql_functor(work)->GetPqxxResult().affected_rows();
}


unsigned long Querist::Impl::Delete(pqxx::transaction_base& work,
                                    const string& rel_name,
                                    const WhereSpecifiers& where_specifiers)
{
    QueryFamily query_family(QueryFamily::DELETE, rel_name);
    SqlFunctor<WhereSpecifiers>
        sql_functor(*this, query_family, Values(), where_specifiers);
    return sql_functor(work)->GetPqxxResult().affected_rows();
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


unsigned long Querist::Count(pqxx::transaction_base& work,
                             const string& query_str,
                             const Values& params,
                             const Specifiers& specifiers)
{
    return pimpl_->Count(work, query_str, params, specifiers);
}


unsigned long Querist::Update(pqxx::transaction_base& work,
                              const string& rel_name,
                              const StringMap& field_expr_map,
                              const Values& params,
                              const WhereSpecifiers& where_specifiers)
{
    return pimpl_->Update(work, rel_name, field_expr_map,
                          params, where_specifiers);
}


unsigned long Querist::Delete(pqxx::transaction_base& work,
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
