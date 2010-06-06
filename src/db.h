
// (c) 2008-2010 by Anton Korenyushkin

#ifndef DB_H
#define DB_H

#include "common.h"

#include <boost/scoped_ptr.hpp>
#include <boost/variant.hpp>


namespace ku
{
    ////////////////////////////////////////////////////////////////////////////
    // ForeignKey, ForeignKeySet, and UniqueKeySet
    ////////////////////////////////////////////////////////////////////////////

    struct ForeignKey {
        StringSet key_attr_names;
        std::string ref_rel_var_name;
        StringSet ref_attr_names;

        ForeignKey(const StringSet& key_attr_names,
                   const std::string& ref_rel_var_name,
                   const StringSet& ref_attr_names)
            : key_attr_names(key_attr_names)
            , ref_rel_var_name(ref_rel_var_name)
            , ref_attr_names(ref_attr_names) {}

        bool operator==(const ForeignKey& other) const {
            return (ref_rel_var_name == other.ref_rel_var_name &&
                    key_attr_names == other.key_attr_names &&
                    ref_attr_names == other.ref_attr_names);
        }
    };

    
    typedef orset<ForeignKey> ForeignKeySet;
    typedef orset<StringSet> UniqueKeySet;
        
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
                 Type type,
                 Type::Trait trait = Type::COMMON,
                 const Value* default_ptr = 0);

        const Attr& GetAttr() const;
        const std::string& GetName() const;
        Type GetType() const;
        Type::Trait GetTrait() const;
        const Value* GetDefaultPtr() const;
        void SetDefaultPtr(const Value* default_ptr);

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

        const UniqueKeySet&
        GetUniqueKeySet(const std::string& rel_var_name) const;

        const ForeignKeySet&
        GetForeignKeySet(const std::string& rel_var_name) const;
        
        void Create(const std::string& name,
                    const RichHeader& rich_header,
                    const UniqueKeySet& unique_key_set,
                    const ForeignKeySet& foreign_keys,
                    const Strings& checks);
        
        void Drop(const StringSet& rel_var_names);
        
        QueryResult Query(const std::string& query,
                          const Drafts& query_params = Drafts(),
                          const Strings& by_exprs = Strings(),
                          const Drafts& by_params = Drafts(),
                          size_t start = 0,
                          size_t length = MINUS_ONE) const;

        size_t Count(const std::string& query,
                     const Drafts& params = Drafts()) const;
        
        size_t Update(const std::string& rel_var_name,
                      const std::string& where,
                      const Drafts& where_params,
                      const StringMap& expr_map,
                      const Drafts& expr_params = Drafts());

        size_t Delete(const std::string& rel_var_name,
                      const std::string& where,
                      const Drafts& params = Drafts());
        
        Values Insert(const std::string& rel_var_name,
                      const DraftMap& draft_map);

        void AddAttrs(const std::string& rel_var_name,
                      const RichHeader& rich_attrs);
        
        void DropAttrs(const std::string& rel_var_name,
                       const StringSet& attr_names);

        void AddDefault(const std::string& rel_var_name,
                        const DraftMap& draft_map);

        void DropDefault(const std::string& rel_var_name,
                         const StringSet& attr_names);

        void AddConstrs(const std::string& rel_var_name,
                        const UniqueKeySet& unique_key_set,
                        const ForeignKeySet& foreign_key_set,
                        const Strings& checks);

        void DropAllConstrs(const std::string& rel_var_name);
        
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
