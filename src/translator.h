
// (c) 2008 by Anton Korenyushkin

/// \file translator.h
/// Ku-to-SQL translator interface

#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#include "common.h"

#include <string>
#include <vector>
#include <map>


namespace ku
{
    /// Database view provider. Translator accesses database through an instance
    /// of subclass of this type.
    class DBViewer {
    public:
        struct RelFields {
            std::string rel_name;
            StringSet field_names;

            RelFields(const std::string& rel_name, const StringSet& field_names)
                : rel_name(rel_name), field_names(field_names) {}
        };
        
        virtual ~DBViewer() {}

        virtual
        const Header& GetRelHeader(const std::string& rel_name) const = 0;
        
        virtual
        std::string Quote(const PgLiter& pg_liter) const = 0;

        virtual
        RelFields GetReference(const RelFields& key) const = 0;
    };


    struct TranslateItem {
        std::string ku_str;
        Types param_types;
        size_t param_shift;
        Strings param_strings;

        explicit TranslateItem(const std::string& ku_str,
                               const Types& param_types = Types(),
                               size_t param_shift = 0,
                               const Strings& param_strings = Strings())
            : ku_str(ku_str)
            , param_types(param_types)
            , param_shift(param_shift)
            , param_strings(param_strings) {}
    };


    typedef std::vector<TranslateItem> TranslateItems;


    struct Translation {
        std::string sql_str;
        Header header;

        Translation(const std::string& sql_str, const Header& header)
            : sql_str(sql_str), header(header) {}
    };


    /// Translator class. Manages translation from ku language to SQL.
    class Translator {
    public:
        explicit Translator(const DBViewer& db_viewer);

        Translation TranslateQuery(
            const TranslateItem& query_item,
            const TranslateItems& where_items = TranslateItems(),
            const TranslateItems& by_items = TranslateItems(),
            const StringSet* only_fields_ptr = 0) const;
        
        std::string TranslateUpdate(
            const TranslateItem& update_item,
            const StringMap& field_expr_map,
            const TranslateItems& where_items = TranslateItems()) const;

        std::string TranslateDelete(
            const std::string& rel_name,
            const TranslateItems& where_items = TranslateItems()) const;

        std::string TranslateExpr(
            const std::string& ku_expr_str,
            const std::string& rel_name,
            const Header& rel_header) const;
                       
    private:
        const DBViewer& db_viewer_;
    };
}

#endif // TRANSLATOR_H
