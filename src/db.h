
// (c) 2008 by Anton Korenyushkin

/// \file db.h
/// Database access interface

#ifndef DB_H
#define DB_H

#include "common.h"

#include <boost/shared_ptr.hpp>
#include <boost/variant.hpp>


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

        explicit OnlySpecifier(const StringSet& field_names)
            : field_names(field_names) {}
    };


    struct WindowSpecifier {
        static const unsigned long ALL = static_cast<unsigned long>(-1);
        
        unsigned long offset;
        unsigned long limit;

        WindowSpecifier(unsigned long offset, unsigned long limit)
            : offset(offset), limit(limit) {}
    };


    typedef boost::variant<
        WhereSpecifier,
        BySpecifier,
        OnlySpecifier,
        WindowSpecifier>
    Specifier;

    
    typedef std::vector<Specifier> Specifiers;

    ////////////////////////////////////////////////////////////////////////////
    // Constrs
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
        std::string ref_rel_var_name;
        StringSet ref_field_names;

        ForeignKey(const StringSet& key_field_names,
                   const std::string& ref_rel_var_name,
                   const StringSet& ref_field_names)
            : key_field_names(key_field_names)
            , ref_rel_var_name(ref_rel_var_name)
            , ref_field_names(ref_field_names) {}
        
        bool operator==(const ForeignKey& other) const {
            return (key_field_names == other.key_field_names &&
                    ref_rel_var_name == other.ref_rel_var_name &&
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
        class Impl;

        QueryResult(const Impl* impl_ptr);
        ~QueryResult();
        size_t GetSize() const;
        std::auto_ptr<Values> GetValuesPtr(size_t idx) const;
        const Header& GetHeader() const;
        size_t GetMemoryUsage() const;
        
    private:
        boost::shared_ptr<const Impl> impl_ptr_;
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


    struct App {
        std::string admin;
        Strings developers;
        std::string email;
        std::string summary;
        std::string description;
        Strings labels;

        App(const std::string& admin,
            const Strings& developers,
            const std::string& email,
            const std::string& summary,
            const std::string& description,
            const Strings& labels)
            : admin(admin)
            , developers(developers)
            , email(email)
            , summary(summary)
            , description(description)
            , labels(labels)
            {}
    };

    
    /// Transaction means of database operations
    class Access {
    public:
        struct Data;
        
        explicit Access(Data& data);
        StringSet GetRelVarNames() const;
        bool HasRelVar(const std::string& rel_var_name) const;

        const RichHeader&
        GetRelVarRichHeader(const std::string& rel_var_name) const;

        /// Relation constraints do not save their order after
        /// store/load from DB. Check constraints are not restored at all.
        const Constrs& GetRelVarConstrs(const std::string& rel_var_name) const;
        
        void CreateRelVar(const std::string& name,
                          const RichHeader& rich_header,
                          const Constrs& constrs);
        
        void DropRelVars(const StringSet& rel_var_names);
        void DropRelVar(const std::string& rel_var_name);
        
        QueryResult Query(const std::string& query_str,
                          const Values& params,
                          const Specifiers& specifiers) const;

        unsigned long Count(const std::string& query_str,
                            const Values& params,
                            const Specifiers& specifiers) const;

        unsigned long Update(const std::string& rel_var_name,
                             const StringMap& field_expr_map,
                             const Values& params,
                             const WhereSpecifiers& where_specifiers);

        unsigned long Delete(const std::string& rel_var_name,
                             const WhereSpecifiers& where_specifiers);
        
        Values Insert(const std::string& rel_var_name,
                      const ValueMap& value_map);

        void CheckAppExists(const std::string& name) const;
        App DescribeApp(const std::string& name) const;
        void CheckUserExists(const std::string& name) const;
        Strings GetAdminedApps(const std::string& user_name) const;
        Strings GetDevelopedApps(const std::string& user_name) const;
        Strings GetAppsByLabel(const std::string& label_name) const;

    private:
        Data& data_;
    };
}

#endif // DB_H
