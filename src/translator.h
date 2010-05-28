
// (c) 2008-2010 by Anton Korenyushkin

#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#include "common.h"


namespace ku
{
    // Database view provider. Translator accesses database through an instance
    // of subclass of this type.
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

        virtual
        const Header& GetHeader(const std::string& rel_var_name) const = 0;
        
        virtual
        std::string Quote(const PgLiter& pg_liter) const = 0;

        virtual
        RelVarFields GetReference(const RelVarFields& key) const = 0;
    };


    class Translator {
    public:
        explicit Translator(const DBViewer& db_viewer);

        std::string TranslateQuery(Header& header,
                                   const std::string& query,
                                   const Values& query_params = Values(),
                                   const Strings& by_exprs = Strings(),
                                   const Values& by_params = Values(),
                                   size_t start = 0,
                                   size_t length = MINUS_ONE) const;
        
        std::string TranslateCount(const std::string& query,
                                   const Values& params) const;

        std::string TranslateUpdate(const std::string& rel_var_name,
                                    const std::string& where,
                                    const Values& where_params,
                                    const StringMap& field_expr_map,
                                    const Values& expr_params) const;

        std::string TranslateDelete(const std::string& rel_var_name,
                                    const std::string& where,
                                    const Values& params) const;

        std::string TranslateExpr(const std::string& expr,
                                  const std::string& rel_var_name,
                                  const Header& header) const;
                       
    private:
        const DBViewer& db_viewer_;
    };
}

#endif // TRANSLATOR_H
