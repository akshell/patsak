
// (c) 2008-2010 by Anton Korenyushkin

#ifndef DB_H
#define DB_H

#include "common.h"

#include <boost/shared_ptr.hpp>
#include <boost/variant.hpp>


namespace ku
{
    ////////////////////////////////////////////////////////////////////////////
    // Constrs
    ////////////////////////////////////////////////////////////////////////////

    struct Unique {
        StringSet field_names;

        explicit Unique(const StringSet& field_names)
            : field_names(field_names) {}
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
    };


    struct Check {
        std::string expr_str;

        explicit Check(const std::string& expr_str)
            : expr_str(expr_str) {}
    };


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


    class DB {
    public:
        DB(const std::string& opt,
           const std::string& schema_name,
           const std::string& app_name);
        ~DB();
        uint64_t GetDBQuota() const;
        uint64_t GetFSQuota() const;
        
    private:
        class Impl;
        friend class Access;
        
        boost::scoped_ptr<Impl> pimpl_;
    };

    ////////////////////////////////////////////////////////////////////////////
    // Access
    ////////////////////////////////////////////////////////////////////////////
    
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
        void SetDefault(const Value& value);

    private:
        Attr attr_;
        Type::Trait trait_;
        boost::shared_ptr<Value> default_ptr_;
    };

    
    typedef orset<RichAttr, ByNameComparator<RichAttr>, ByNameFinder<RichAttr> >
    RichHeader;


    struct QueryResult {
        Header header;
        std::vector<Values> tuples;

        QueryResult(const Header& header, const std::vector<Values>& tuples)
            : header(header), tuples(tuples) {}
    };


    struct App {
        std::string admin;
        Strings developers;
        std::string summary;
        std::string description;
        Strings labels;

        App(const std::string& admin,
            const Strings& developers,
            const std::string& summary,
            const std::string& description,
            const Strings& labels)
            : admin(admin)
            , developers(developers)
            , summary(summary)
            , description(description)
            , labels(labels)
            {}
    };


    class Access {
    public:
        explicit Access(DB& db);
        ~Access();
        
        void Commit();

        StringSet GetNames() const;

        const RichHeader& GetRichHeader(const std::string& rel_var_name) const;

        // Relation constraints do not save their order after
        // store/load from DB. Check constraints are not restored at all.
        const Constrs& GetConstrs(const std::string& rel_var_name) const;
        
        void Create(const std::string& name,
                    const RichHeader& rich_header,
                    const Constrs& constrs);
        
        void Drop(const StringSet& rel_var_names);
        
        QueryResult Query(const std::string& query,
                          const Values& query_params = Values(),
                          const Strings& by_exprs = Strings(),
                          const Values& by_params = Values(),
                          size_t start = 0,
                          size_t length = MINUS_ONE) const;

        size_t Count(const std::string& query,
                     const Values& params = Values()) const;

        size_t Update(const std::string& rel_var_name,
                      const std::string& where,
                      const Values& where_params,
                      const StringMap& field_expr_map,
                      const Values& expr_params = Values());

        size_t Delete(const std::string& rel_var_name,
                      const std::string& where,
                      const Values& params = Values());
        
        Values Insert(const std::string& rel_var_name,
                      const ValueMap& value_map);

        void AddAttrs(const std::string& rel_var_name,
                      const RichHeader& rich_attrs);
        
        void DropAttrs(const std::string& rel_var_name,
                       const StringSet& attr_names);

        void SetDefault(const std::string& rel_var_name,
                        const ValueMap& value_map);
        
        std::string GetAppPatsakVersion(const std::string& name) const;
        void CheckAppExists(const std::string& name) const;
        App DescribeApp(const std::string& name) const;
        std::string GetUserEmail(const std::string& user_name) const;
        void CheckUserExists(const std::string& name) const;
        Strings GetAdminedApps(const std::string& user_name) const;
        Strings GetDevelopedApps(const std::string& user_name) const;
        Strings GetAppsByLabel(const std::string& label_name) const;

    private:
        class WorkWrapper;
        
        DB::Impl& db_impl_;
        boost::scoped_ptr<WorkWrapper> work_ptr_;
    };
}

#endif // DB_H
