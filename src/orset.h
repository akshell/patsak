
// (c) 2009-2010 by Anton Korenyushkin

#ifndef ORSET_H
#define ORSET_H

#include "debug.h"

#include <vector>
#include <algorithm>
#include <stdexcept>
#include <memory>


namespace ku
{
    template <typename T, typename CompT = std::equal_to<T> >
    class item_finder : public std::binary_function<T, T, bool> {
    public:
        bool operator()(const T& item, const T& key) const {
            return CompT()(item, key);
        }

        void not_found(const T& /*key*/) const {}
    };


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
        using base::erase;

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

        T* find_ptr(const typename FindT::second_argument_type& key) {
            typename base::iterator itr(
                std::find_if(begin(), end(), std::bind2nd(FindT(), key)));
            return itr == end() ? 0 : &*itr;
        }

        T& find(const typename FindT::second_argument_type& key) {
            T* ptr = find_ptr(key);
            if (!ptr) {
                FindT().not_found(key);
                throw std::runtime_error("key not found");
            }
            return *ptr;
        }

        const T*
        find_ptr(const typename FindT::second_argument_type& key) const {
            typename base::const_iterator itr(
                std::find_if(begin(), end(), std::bind2nd(FindT(), key)));
            return itr == end() ? 0 : &*itr;
        }

        const T& find(const typename FindT::second_argument_type& key) const {
            const T* ptr = find_ptr(key);
            if (!ptr) {
                FindT().not_found(key);
                throw std::runtime_error("key not found");
            }
            return *ptr;
        }
    };
}

#endif // ORSET_H
