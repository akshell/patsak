
// (c) 2008 by Anton Korenyushkin

/// \file db.h
/// Database access interface

#ifndef DB_H
#define DB_H

#include "common.h"

#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/variant.hpp>

#include <string>
#include <vector>


namespace ku
{
    ////////////////////////////////////////////////////////////////////////////
    // Specifiers
    ////////////////////////////////////////////////////////////////////////////

    struct WhereSpecifier {
        std::string expr_str;
        Values params;

        WhereSpecifier(const std::string& expr_str, const Values& params)
            : expr_str(expr_str), params(params) {}
    };


    typedef std::vector<WhereSpecifier> WhereSpecifiers;


    struct BySpecifier {
        std::string expr_str;
        Values params;

        BySpecifier(const std::string& expr_str, const Values& params)
            : expr_str(expr_str), params(params) {}
    };


    struct OnlySpecifier {
        StringSet field_names;

        OnlySpecifier(const StringSet& field_names)
            : field_names(field_names) {}
    };


    typedef boost::variant<
        WhereSpecifier,
        BySpecifier,
        OnlySpecifier>
    Specifier;

    
    typedef std::vector<Specifier> Specifiers;

    ////////////////////////////////////////////////////////////////////////////
    /// Constr
    ////////////////////////////////////////////////////////////////////////////

    struct Unique {
        StringSet field_names;

        explicit Unique(const StringSet& field_names)
            : field_names(field_names) {}

        bool operator==(const Unique& other) const {
            return field_names == other.field_names;
        }
    };
    

    struct ForeignKey {
        StringSet key_field_names;
        std::string ref_rel_name;
        StringSet ref_field_names;

        ForeignKey(const StringSet& key_field_names,
                   const std::string& ref_rel_name,
                   const StringSet& ref_field_names)
            : key_field_names(key_field_names)
            , ref_rel_name(ref_rel_name)
            , ref_field_names(ref_field_names) {}
        
        bool operator==(const ForeignKey& other) const {
            return (key_field_names == other.key_field_names &&
                    ref_rel_name == other.ref_rel_name &&
                    ref_field_names == other.ref_field_names);
        }        
    };


    struct Check {
        std::string expr_str;

        explicit Check(const std::string& expr_str)
            : expr_str(expr_str) {}

        bool operator==(const Check& other) const {
            return expr_str == other.expr_str;
        }        
    };


    /// Table constraints representation
    typedef boost::variant<
        Unique,
        ForeignKey,
        Check>
    Constr;

    
    typedef std::vector<Constr> Constrs;
        
    ////////////////////////////////////////////////////////////////////////////
    // DB
    ////////////////////////////////////////////////////////////////////////////
    
    class Access;


    /// Database transaction function interface
    class Transactor {
    public:
        virtual void operator()(Access& access) = 0;
        virtual void Reset() {}
        virtual ~Transactor() {}
    };
    

    /// Database entry point
    class DB {
    public:
        DB(const std::string& opt, const std::string& schema_name);
        ~DB();
        void Perform(Transactor& transactor);
        
    private:
        class Impl;
        
        boost::scoped_ptr<Impl> pimpl_;
    };

    ////////////////////////////////////////////////////////////////////////////
    // Access
    ////////////////////////////////////////////////////////////////////////////
    
    /// Result of successful query, i.e. a list of tuples
    class QueryResult {
    public:
        struct Data;

        QueryResult(Data* data_ptr);
        ~QueryResult();
        size_t GetSize() const;
        Values GetValues(size_t idx) const;
        const Header& GetHeader() const;
        
    private:
        boost::shared_ptr<Data> data_ptr_;
    };


    class RichAttr {
    public:
        RichAttr(const std::string& name,
                 const Type& type,
                 Type::Trait trait = Type::COMMON,
                 const Value* default_ptr = 0);

        const Attr& GetAttr() const;
        const std::string& GetName() const;
        Type GetType() const;
        Type::Trait GetTrait() const;
        const Value* GetDefaultPtr() const;

    private:
        Attr attr_;
        Type::Trait trait_;
        boost::shared_ptr<Value> default_ptr_;
    };

    
    typedef orset<RichAttr, ByNameComparator<RichAttr>, ByNameFinder<RichAttr> >
    RichHeader;

    
    /// Transaction means of database operations
    class Access {
    public:
        struct Data;
        
        explicit Access(Data& data);
        StringSet GetRelNames() const;
        bool HasRel(const std::string& rel_name) const;
        const RichHeader& GetRelRichHeader(const std::string& rel_name) const;

        /// Relation constraints do not save their order after
        /// store/load from DB. Check constraints are not restored at all.
        const Constrs& GetRelConstrs(const std::string& rel_name) const;
        
        void CreateRel(const std::string& name,
                       const RichHeader& rich_header,
                       const Constrs& constrs);
        
        void DeleteRels(const StringSet& rel_names);
        void DeleteRel(const std::string& rel_name);
        
        QueryResult Query(const std::string& query_str,
                          const Values& params,
                          const Specifiers& specifiers) const;

        void Update(const std::string& rel_name,
                    const StringMap& field_expr_map,
                    const Values& params,
                    const WhereSpecifiers& where_specifiers);

        void Delete(const std::string& rel_name,
                    const WhereSpecifiers& where_specifiers);
        
        void Insert(const std::string& rel_name, const ValueMap& value_map);

    private:
        Data& data_;
    };
}

#endif // DB_H
