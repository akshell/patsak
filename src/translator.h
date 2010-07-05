
// (c) 2008-2010 by Anton Korenyushkin

#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#include "common.h"


namespace ak
{
    typedef const Header& (*GetHeaderCallback)(const std::string& rel_var_name);

    typedef void (*FollowReferenceCallback)(const std::string& key_rel_var_name,
                                            const StringSet& key_attr_names,
                                            std::string& ref_rel_var_name,
                                            StringSet& ref_attr_names);

    typedef std::string (*QuoteCallback)(const PgLiter& pg_liter);


    std::string TranslateQuery(Header& header,
                               const std::string& query,
                               const Drafts& query_params = Drafts(),
                               const Strings& by_exprs = Strings(),
                               const Drafts& by_params = Drafts(),
                               size_t start = 0,
                               size_t length = MINUS_ONE);

    std::string TranslateCount(const std::string& query,
                               const Drafts& params);

    std::string TranslateUpdate(const std::string& rel_var_name,
                                const std::string& where,
                                const Drafts& where_params,
                                const StringMap& expr_map,
                                const Drafts& expr_params);

    std::string TranslateDelete(const std::string& rel_var_name,
                                const std::string& where,
                                const Drafts& params);

    std::string TranslateExpr(const std::string& expr,
                              const std::string& rel_var_name,
                              const Header& header);


    void InitTranslator(GetHeaderCallback get_header_cb,
                        FollowReferenceCallback follow_reference_cb,
                        QuoteCallback quote_cb);
}

#endif // TRANSLATOR_H
