
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
        struct RelVarAttrs {
            std::string rel_var_name;
            StringSet attr_names;

            RelVarAttrs(const std::string& rel_var_name,
                         const StringSet& attr_names)
                : rel_var_name(rel_var_name), attr_names(attr_names) {}
        };

        virtual ~DBViewer() {}

        virtual
        const Header& GetHeader(const std::string& rel_var_name) const = 0;

        virtual
        std::string Quote(const PgLiter& pg_liter) const = 0;

        virtual
        RelVarAttrs GetReference(const RelVarAttrs& key) const = 0;
    };


    class Translator {
    public:
        explicit Translator(const DBViewer& db_viewer);

        std::string TranslateQuery(Header& header,
                                   const std::string& query,
                                   const Drafts& query_params = Drafts(),
                                   const Strings& by_exprs = Strings(),
                                   const Drafts& by_params = Drafts(),
                                   size_t start = 0,
                                   size_t length = MINUS_ONE) const;

        std::string TranslateCount(const std::string& query,
                                   const Drafts& params) const;

        std::string TranslateUpdate(const std::string& rel_var_name,
                                    const std::string& where,
                                    const Drafts& where_params,
                                    const StringMap& expr_map,
                                    const Drafts& expr_params) const;

        std::string TranslateDelete(const std::string& rel_var_name,
                                    const std::string& where,
                                    const Drafts& params) const;

        std::string TranslateExpr(const std::string& expr,
                                  const std::string& rel_var_name,
                                  const Header& header) const;

    private:
        const DBViewer& db_viewer_;
    };
}

#endif // TRANSLATOR_H
