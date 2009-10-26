
// (c) 2008 by Anton Korenyushkin

/// \file translator.h
/// Ku-to-SQL translator interface

#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#include "common.h"


namespace ku
{
    /// Database view provider. Translator accesses database through an instance
    /// of subclass of this type.
    class DBViewer {
    public:
        struct RelVarFields {
            std::string rel_var_name;
            StringSet field_names;

            RelVarFields(const std::string& rel_var_name,
                         const StringSet& field_names)
                : rel_var_name(rel_var_name), field_names(field_names) {}
        };
        
        virtual ~DBViewer() {}

        virtual const Header&
        GetRelVarHeader(const std::string& rel_var_name) const = 0;
        
        virtual
        std::string Quote(const PgLiter& pg_liter) const = 0;

        virtual
        RelVarFields GetReference(const RelVarFields& key) const = 0;
    };


    const size_t RAW_SHIFT = static_cast<size_t>(-1);
    

    struct TranslateItem {
        std::string ku_str;
        Types param_types;
        size_t param_shift;
        Strings param_strings;
        
        TranslateItem(const std::string& ku_str,
                      const Types& param_types = Types(),
                      size_t param_shift = 0)
            : ku_str(ku_str)
            , param_types(param_types)
            , param_shift(param_shift) {}

        TranslateItem(const std::string& ku_str,
                      const Types& param_types,
                      const Strings& param_strings)
            : ku_str(ku_str)
            , param_types(param_types)
            , param_shift(RAW_SHIFT)
            , param_strings(param_strings) {
            KU_ASSERT(param_types.size() == param_strings.size());
        }
    };


    typedef std::vector<TranslateItem> TranslateItems;


    struct Window {
        static const unsigned long ALL = static_cast<unsigned long>(-1);

        size_t param_shift;
        unsigned long offset;
        unsigned long limit;

        explicit Window(size_t param_shift)
            : param_shift(param_shift) {}

        Window(unsigned long offset, unsigned long limit)
            : param_shift(RAW_SHIFT), offset(offset), limit(limit) {}
    };


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
            const StringSet* only_fields_ptr = 0,
            const Window* window_ptr = 0) const;
        
        std::string TranslateCount(
            const TranslateItem& query_item,
            const TranslateItems& where_items = TranslateItems(),
            const Window* window_ptr = 0) const;

        std::string TranslateUpdate(
            const TranslateItem& update_item,
            const StringMap& field_expr_map,
            const TranslateItems& where_items = TranslateItems()) const;

        std::string TranslateDelete(
            const std::string& rel_var_name,
            const TranslateItems& where_items = TranslateItems()) const;

        std::string TranslateExpr(
            const std::string& ku_expr_str,
            const std::string& rel_var_name,
            const Header& rel_header) const;
                       
    private:
        const DBViewer& db_viewer_;
    };
}

#endif // TRANSLATOR_H
