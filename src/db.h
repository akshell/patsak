
// (c) 2008-2010 by Anton Korenyushkin

#ifndef DB_H
#define DB_H

#include "common.h"

#include <boost/scoped_ptr.hpp>
#include <boost/variant.hpp>


namespace ak
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
    // RichAttr and RichHeader
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

    ////////////////////////////////////////////////////////////////////////////
    // API
    ////////////////////////////////////////////////////////////////////////////

    void Commit();
    void RollBack();
    StringSet GetRelVarNames();
    const RichHeader& GetRichHeader(const std::string& rel_var_name);
    const UniqueKeySet& GetUniqueKeySet(const std::string& rel_var_name);
    const ForeignKeySet& GetForeignKeySet(const std::string& rel_var_name);

    void CreateRelVar(const std::string& name,
                      const RichHeader& rich_header,
                      const UniqueKeySet& unique_key_set,
                      const ForeignKeySet& foreign_keys,
                      const Strings& checks);

    void DropRelVars(const StringSet& rel_var_names);

    void Query(Header& header,
               std::vector<Values>& tuples,
               const std::string& query,
               const Drafts& query_params = Drafts(),
               const Strings& by_exprs = Strings(),
               const Drafts& by_params = Drafts(),
               size_t start = 0,
               size_t length = MINUS_ONE);

    size_t Count(const std::string& query, const Drafts& params = Drafts());

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

    void InitDatabase(const std::string& options,
                      const std::string& schema_name,
                      const std::string& tablespace_name);
}

#endif // DB_H
