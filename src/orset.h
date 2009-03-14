
// (c) 2009 by Anton Korenyushkin

/// \file orset.h
/// Ordered set interface and impl

#ifndef ORSET_H
#define ORSET_H


#include "error.h"

#include <vector>
#include <functional>
#include <algorithm>


namespace ku
{
    template <typename T, typename CompT = std::equal_to<T> >
    class item_finder : public std::binary_function<T, T, bool> {
    public:
        bool operator()(const T& item, const T& key) const {
            return CompT()(item, key);
        }

        std::exception not_found_exception(const T& /*key*/) const {
            return std::runtime_error("item_finder: key not found");
        }
    };

    
    /// Ordered set container
    template <typename T,
              typename CompT = std::equal_to<T>,
              typename FindT = item_finder<T, CompT> >
    class orset : private std::vector<T> {
    private:
        typedef std::vector<T> base;
        typedef orset<T, CompT, FindT> this_type;
        
    public:
        using base::iterator;
        using base::const_iterator;
        using base::size_type;
        using base::value_type;
        using base::reverse_iterator;
        using base::const_reverse_iterator;
        
        using base::begin;
        using base::end;
        using base::rbegin;
        using base::rend;
        using base::size;
        using base::at;
        using base::operator[];
        using base::back;
        using base::capacity;
        using base::clear;
        using base::empty;
        using base::front;
        using base::reserve;

        orset() {}

        template <typename ItrT>
        orset(ItrT from, ItrT to, bool sure = true) {
            while (from != to)
                if (sure)
                    add_sure(*from++);
                else
                    add_unsure(*from++);
        }

        void swap(orset& other) {
            base::swap(other);
        }

        bool contains(const T& val) const {
            return std::find_if(begin(),
                                end(),
                                std::bind1st(CompT(), val)) != end();
        }

        bool add_unsure(const T& val) {
            if (contains(val))
                return false;
            push_back(val);
            return true;
        }

        void add_sure(const T& val) {
            KU_ASSERT(!contains(val));
            push_back(val);
        }

        bool operator==(const this_type& other) const {
            if (size() != other.size())
                return false;
            for (typename base::const_iterator itr = begin();
                 itr != end();
                 ++itr)
                if (std::find_if(other.begin(),
                                 other.end(),
                                 std::bind1st(CompT(), *itr)) == other.end())
                    return false;
            return true;
        }

        bool operator!=(const this_type& other) const {
            return !(*this == other);
        }

        T& find(const typename FindT::second_argument_type& key) {
            typename base::iterator
                itr(std::find_if(begin(), end(), std::bind2nd(FindT(), key)));
            if (itr == end())
                throw FindT().not_found_exception(key);
            return *itr;
        }

        const T& find(const typename FindT::second_argument_type& key) const {
            typename base::const_iterator
                itr(std::find_if(begin(), end(), std::bind2nd(FindT(), key)));
            if (itr == end())
                throw FindT().not_found_exception(key);
            return *itr;
        }
    };
}

#endif // ORSET_H
