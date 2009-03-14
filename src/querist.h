
// (c) 2008 by Anton Korenyushkin

/// \file querist.h
/// Database query interface

#ifndef QUERIST_H
#define QUERIST_H

#include "db.h"

#include <pqxx/pqxx>


namespace ku
{
    class DBViewer;


    /// Functor qouting strings for use as PG literals
    class Quoter {
    public:
        explicit Quoter(pqxx::connection& conn)
            : conn_(conn) {}

        explicit Quoter(const pqxx::transaction_base& work)
            : conn_(work.conn()) {}

        std::string operator()(const PgLiter& pg_liter) const {
            return pg_liter.quote_me ? conn_.quote(pg_liter.str) : pg_liter.str;
        }

        std::string operator()(const Value& value) const {
            return (*this)(value.GetPgLiter());
        }

        Strings operator()(const Values& values) const {
            Strings result;
            result.reserve(values.size());
            for (Values::const_iterator itr = values.begin();
                 itr != values.end();
                 ++itr)
                result.push_back((*this)(*itr));
            return result;
        }

    private:
        pqxx::connection_base& conn_;
    };


    /// Caching query maker
    class Querist {
    public:
        Querist(const DBViewer& db_viewer, pqxx::connection& conn);
        ~Querist();
        
        QueryResult Query(pqxx::transaction_base& work,
                          const std::string& query_str,
                          const Values& params,
                          const Specifiers& specifiers);

        void Update(pqxx::transaction_base& work,
                    const std::string& rel_name,
                    const StringMap& field_expr_map,
                    const Values& params,
                    const WhereSpecifiers& where_specifiers);
                    
        void Delete(pqxx::transaction_base& work,
                    const std::string& rel_name,
                    const WhereSpecifiers& where_specifiers);
        
        void ClearCache();

        std::string TranslateExpr(const std::string& ku_expr_str,
                                  const std::string& rel_name,
                                  const Header& rel_header) const;

    private:
        class Impl;

        boost::scoped_ptr<Impl> pimpl_;
    };
}

#endif // QUERIST_H
