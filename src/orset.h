// (c) 2009-2011 by Anton Korenyushkin

#ifndef ORSET_H
#define ORSET_H

#include "debug.h"

#include <boost/foreach.hpp>

#include <vector>


namespace ak
{
    template <typename T>
    struct identity : public std::unary_function<T, T> {
        const T& operator()(const T& value) const {
            return value;
        }
    };


    template <typename T, typename F = identity<T> >
    class orset : private std::vector<T> {
    private:
        typedef std::vector<T> base;
        typedef orset<T, F> this_type;

    public:
        using base::const_iterator;
        using base::empty;
        using base::size;
        using base::clear;
        using base::reserve;

        typename base::const_iterator begin() const {
            return base::begin();
        }

        typename base::const_iterator end() const {
            return base::end();
        }

        const T& operator[](size_t i) const {
            return base::operator[](i);
        }

        void erase(size_t pos) {
            base::erase(base::begin() + pos);
        }

        const T& front() const {
            return base::front();
        }

        const T& back() const {
            return base::back();
        }

        const T* find(const typename F::result_type& key) const {
            BOOST_FOREACH(const T& value, *this)
                if (F()(value) == key)
                    return &value;
            return 0;
        }

        void add(const T& value) {
            AK_ASSERT(!find(F()(value)));
            push_back(value);
        }

        bool add_safely(const T& value) {
            if (find(F()(value)))
                return false;
            push_back(value);
            return true;
        }

        bool operator==(const this_type& other) const {
            if (size() != other.size())
                return false;
            BOOST_FOREACH(const T& value, other)
                if (!find(F()(value)))
                    return false;
            return true;
        }

        bool operator!=(const this_type& other) const {
            return !(*this == other);
        }
    };
}


namespace boost
{
    template <typename T, typename F>
    struct range_mutable_iterator<ak::orset<T, F> >
    {
        typedef typename ak::orset<T, F>::const_iterator type;
    };
}

#endif // ORSET_H
